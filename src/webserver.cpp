#include "webserver.h"
#include "display.h"
#include "squeezeapi.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <stdarg.h>
#include <vector>
#include <HTTPClient.h>

// ─── Webserver-Log (Ring-Buffer, abrufbar unter /log) ────────────────────────

#define LOG_LINES    80
#define LOG_LINE_LEN 128
static char    s_log[LOG_LINES][LOG_LINE_LEN];
static uint8_t s_logHead = 0;
static portMUX_TYPE s_logMux = portMUX_INITIALIZER_UNLOCKED;

void srvLog(const char* fmt, ...) {
    portENTER_CRITICAL(&s_logMux);
    va_list ap; va_start(ap, fmt);
    vsnprintf(s_log[s_logHead], LOG_LINE_LEN, fmt, ap);
    va_end(ap);
    s_logHead = (s_logHead + 1) % LOG_LINES;
    portEXIT_CRITICAL(&s_logMux);
}

// ─── Globale Instanzen ───────────────────────────────────────────────────────

static AsyncWebServer   server(80);
static DNSServer        dnsServer;
static WifiState        g_wifiState   = WIFI_STATE_DISCONNECTED;
static uint32_t         g_restartAt   = 0;
static uint32_t         g_lastPoll    = 0;
static uint32_t         g_reconnectAt = 0;
static uint32_t         g_wifiConnectedAt = 0;
static uint32_t         g_lastLmsOkAt = 0;
static uint8_t          g_lmsFailStreak = 0;
static SqueezieStatus   g_status;
static SemaphoreHandle_t g_statusMux   = nullptr; // schützt g_status (Core0↔Core1)
static volatile bool     g_forceRepoll = false;   // sofortiger Re-Poll nach Steuerbefehl
static volatile bool     g_userCommandActive = false;
static volatile uint32_t g_fastPollUntil = 0;
static volatile bool     g_otaActive   = false;   // lmsPoll + Cover-DL pausieren während OTA
static int               g_lastOtaPct  = -1;      // Throttle: UI-Status nur bei %-Änderung
static volatile int      g_otaUiPct    = -1;      // OTA-Fortschritt, Anzeige im main loop
static volatile bool     g_otaUiDirty  = false;   // OTA-Screen im main loop sicher neu zeichnen
static int               g_otaError    = 0;       // letzter OTA-Fehlercode
static bool              g_otaRejected = false;   // Upload-Datei vor Update.write abgelehnt
static bool              g_loopWdtPausedForOta = false;
static uint32_t          g_lastSyncReconcile = 0; // Sync-Volumen nur in Intervallen prüfen
static constexpr size_t   WEB_COVER_BUF_SIZE = 196608;
static constexpr uint32_t  WEB_COVER_RETRY_DELAY_MS = 15000;
static constexpr uint32_t  WEB_COVER_TASK_TIMEOUT_MS = 8000;
static uint8_t*           g_webCoverBuf = nullptr;
static volatile size_t    g_webCoverLen = 0;
static SemaphoreHandle_t  g_webCoverMux = nullptr;
static TaskHandle_t       g_webCoverTask = nullptr;
static String             g_webPendingCoverUrl;
static String             g_webPendingCoverKey;
static String             g_webLoadedCoverKey;
static volatile uint32_t  g_webCoverRetryAt = 0;
static volatile uint32_t  g_webCoverTaskStartedAt = 0;
static volatile uint32_t  g_webCoverSerial = 0;

// ─── Konfigurationsstrukturen ────────────────────────────────────────────────

struct NetCfg { String ssid, pass, ip, mask, gw, dns, ntp; bool dhcp = true; };
struct SrvCfg {
    String host, playerMac;
    uint16_t port = LYRION_DEFAULT_PORT;
    int scrollSpeed = 30;
    bool syncVolume = true;
};

static NetCfg g_net;
static SrvCfg g_srv;
static String g_playlistId;
static String g_playlistName;

static constexpr uint32_t LMS_POLL_INTERVAL_MS = 500;
static constexpr uint32_t LMS_FAST_POLL_INTERVAL_MS = 150;
static constexpr uint32_t LMS_FAST_POLL_WINDOW_MS = 1800;
static constexpr uint32_t SYNC_RECONCILE_INTERVAL_MS = 30000;

static void pauseLoopWatchdogForOta() {
    if (g_loopWdtPausedForOta) return;
    if (esp_task_wdt_delete(nullptr) == ESP_OK) {
        g_loopWdtPausedForOta = true;
        srvLog("[OTA] Loop-Watchdog pausiert.");
    }
}

static void resumeLoopWatchdogAfterOta() {
    if (!g_loopWdtPausedForOta) return;
    if (esp_task_wdt_add(nullptr) == ESP_OK) {
        g_loopWdtPausedForOta = false;
        srvLog("[OTA] Loop-Watchdog aktiv.");
    }
}

// ─── NVS ─────────────────────────────────────────────────────────────────────

static void loadCfg() {
    Preferences p; p.begin(NVS_NAMESPACE, true);
    g_net.ssid        = p.getString(NVS_KEY_WIFI_SSID,  "");
    g_net.pass        = p.getString(NVS_KEY_WIFI_PASS,  "");
    g_net.dhcp        = p.getBool  (NVS_KEY_NET_DHCP,   true);
    g_net.ip          = p.getString(NVS_KEY_NET_IP,     "");
    g_net.mask        = p.getString(NVS_KEY_NET_MASK,   "255.255.255.0");
    g_net.gw          = p.getString(NVS_KEY_NET_GW,     "");
    g_net.dns         = p.getString(NVS_KEY_NET_DNS,    "");
    g_net.ntp         = p.getString(NVS_KEY_NET_NTP,    "pool.ntp.org");
    g_srv.host        = p.getString(NVS_KEY_SRV_HOST,   "");
    g_srv.port        = p.getUShort(NVS_KEY_SRV_PORT,   LYRION_DEFAULT_PORT);
    g_srv.playerMac   = p.getString(NVS_KEY_PLAYER_MAC, "");
    g_srv.syncVolume  = p.getBool  (NVS_KEY_SYNC_VOL,   true);
    g_srv.scrollSpeed = p.getInt   (NVS_KEY_SCROLL_SPD, 30);
    g_playlistId      = p.getString(NVS_KEY_PL_ID,      "");
    g_playlistName    = p.getString(NVS_KEY_PL_NAME,    "");
    p.end();
}
static void saveNetCfg() {
    Preferences p; p.begin(NVS_NAMESPACE, false);
    p.putString(NVS_KEY_WIFI_SSID, g_net.ssid); p.putString(NVS_KEY_WIFI_PASS, g_net.pass);
    p.putBool  (NVS_KEY_NET_DHCP,  g_net.dhcp); p.putString(NVS_KEY_NET_IP,    g_net.ip);
    p.putString(NVS_KEY_NET_MASK,  g_net.mask); p.putString(NVS_KEY_NET_GW,    g_net.gw);
    p.putString(NVS_KEY_NET_DNS,   g_net.dns);  p.putString(NVS_KEY_NET_NTP,   g_net.ntp);
    p.end();
}
static void saveSrvCfg() {
    Preferences p; p.begin(NVS_NAMESPACE, false);
    p.putString(NVS_KEY_SRV_HOST,   g_srv.host);  p.putUShort(NVS_KEY_SRV_PORT,   g_srv.port);
    p.putString(NVS_KEY_PLAYER_MAC, g_srv.playerMac); p.putInt(NVS_KEY_SCROLL_SPD, g_srv.scrollSpeed);
    p.putBool  (NVS_KEY_SYNC_VOL,   g_srv.syncVolume);
    p.end();
}
static void savePlaylistCfg() {
    Preferences p; p.begin(NVS_NAMESPACE, false);
    p.putString(NVS_KEY_PL_ID,   g_playlistId);
    p.putString(NVS_KEY_PL_NAME, g_playlistName);
    p.end();
}

void wifiFactoryReset() {
    Preferences p; p.begin(NVS_NAMESPACE, false); p.clear(); p.end();
    ESP.restart();
}

// ─── WiFi ────────────────────────────────────────────────────────────────────

static void resetWiFiStack() {
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_MODE_NULL);
    delay(150);
}

static void startAP() {
    resetWiFiStack();
    WiFi.mode(WIFI_AP);
    delay(150);
    bool ok = WiFi.softAP(WIFI_AP_SSID);
    if (!ok) {
        srvLog("[WiFi] softAP erster Versuch fehlgeschlagen, Retry...");
        delay(250);
        ok = WiFi.softAP(WIFI_AP_SSID);
    }
    if (!ok) {
        g_wifiState = WIFI_STATE_DISCONNECTED;
        srvLog("[WiFi] AP-Start fehlgeschlagen.");
        return;
    }
    dnsServer.start(53, "*", WiFi.softAPIP());
    g_wifiState = WIFI_STATE_AP;
    srvLog("[WiFi] AP-Modus: %s  IP: %s", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}
static bool connectWiFi(uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS) {
    if (g_net.ssid.isEmpty()) return false;
    resetWiFiStack();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    delay(150);
    if (!g_net.dhcp && !g_net.ip.isEmpty()) {
        IPAddress ip, mask, gw, dns;
        ip.fromString(g_net.ip); mask.fromString(g_net.mask);
        gw.fromString(g_net.gw); dns.fromString(g_net.dns.isEmpty() ? g_net.gw : g_net.dns);
        WiFi.config(ip, gw, mask, dns);
    }
    WiFi.begin(g_net.ssid.c_str(), g_net.pass.c_str());
    g_wifiState = WIFI_STATE_CONNECTING;
    srvLog("[WiFi] Verbinde mit '%s'...", g_net.ssid.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        g_wifiState = WIFI_STATE_CONNECTED;
        g_wifiConnectedAt = millis();
        g_lastLmsOkAt = millis();
        g_lmsFailStreak = 0;
        esp_wifi_set_ps(WIFI_PS_NONE);
        int8_t txPow = 0;
        esp_wifi_set_max_tx_power(78);
        esp_wifi_get_max_tx_power(&txPow);    // Rücklesen — war set_max_tx_power wirksam?
        srvLog("[WiFi] Verbunden, IP: %s, TX=%.1fdBm",
               WiFi.localIP().toString().c_str(), txPow * 0.25f);
        configTime(3600, 3600, g_net.ntp.c_str());
        if (!g_srv.host.isEmpty()) squeezeSetServer(g_srv.host, g_srv.port);
        return true;
    }
    g_wifiState = WIFI_STATE_DISCONNECTED;
    srvLog("[WiFi] Verbindung fehlgeschlagen (Status=%d).", (int)WiFi.status());
    return false;
}

// ─── CSS (gemeinsam für alle Seiten) ─────────────────────────────────────────

static const char CSS_COMMON[] PROGMEM = R"CSS(
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#0d0d0d;color:#e0e0e0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-size:20px}
a,a:visited{color:inherit;text-decoration:none}
input[type=text],input[type=password],input[type=number]{
  width:100%;padding:12px 14px;background:#1e1e1e;border:1px solid #444;
  border-radius:8px;color:#e0e0e0;font-size:1em;margin-bottom:12px}
input[type=text]:focus,input[type=password]:focus,input[type=number]:focus{
  outline:none;border-color:#00c853}
.lbl{display:block;font-size:1em;color:#b0b0b0;margin-bottom:6px;margin-top:4px}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.chk{display:flex;align-items:center;gap:10px;font-size:1em;color:#c0c0c0;margin-bottom:12px}
input[type=checkbox]{width:22px;height:22px;accent-color:#00c853;flex-shrink:0}
.btn{display:inline-flex;align-items:center;justify-content:center;border:2px solid #555;
  background:#2a2a2a;color:#e0e0e0;border-radius:8px;padding:14px 20px;
  font-size:1em;cursor:pointer;gap:8px}
.btn:active{opacity:.65}
.btn.primary{background:#00c853;border-color:#00c853;color:#000;font-weight:700}
.btn.full{width:100%;justify-content:center}
.btn.danger{background:#c0392b;border-color:#c0392b;color:#fff}
.card{background:#1a1a1a;border-radius:12px;padding:20px;margin-bottom:16px}
.card h3{font-size:1em;font-weight:700;color:#aaaaaa;text-transform:uppercase;
  letter-spacing:.8px;margin-bottom:16px}
.msg{font-size:1em;color:#808080;margin-top:8px}
.tab-bar{display:flex;background:#111;border-top:1px solid #2a2a2a;
  position:fixed;bottom:0;left:0;right:0;z-index:100}
.tab-item{flex:1;display:flex;flex-direction:column;align-items:center;
  gap:4px;padding:10px 0 8px;color:#888;font-size:1em;text-transform:uppercase;letter-spacing:.5px}
.tab-item.active{color:#00c853}
.tab-item svg{width:28px;height:28px;fill:currentColor}
)CSS";

// ─── AP-Konfigurationsseite ───────────────────────────────────────────────────

static const char PAGE_AP[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang=de>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Squeezie Setup</title>
<style>
)HTML";
static const char PAGE_AP2[] PROGMEM = R"HTML(
body{max-width:520px;margin:0 auto;padding:20px}
.hdr{padding:20px 0 24px;text-align:center}
.hdr h1{font-size:1.6em;color:#00c853;margin-bottom:6px}
.hdr p{font-size:1em;color:#808080}
#sf{display:none}
</style></head><body>
<div class=hdr>
  <h1>Squeezie</h1>
  <p>Ersteinrichtung &mdash; WLAN und Netzwerk konfigurieren</p>
</div>
<div class=card>
  <h3>WLAN</h3>
  <label class=lbl>Netzwerk (SSID)</label>
  <input type=text id=s placeholder="WLAN-Name eingeben">
  <label class=lbl>Passwort</label>
  <input type=password id=p placeholder="WLAN-Passwort">
</div>
<div class=card>
  <h3>IP-Konfiguration</h3>
  <div class=chk><input type=checkbox id=d checked onchange="document.getElementById('sf').style.display=this.checked?'none':'block'">
    <span>DHCP &mdash; IP automatisch beziehen</span></div>
  <div id=sf>
    <div class=row2>
      <div><label class=lbl>IP-Adresse</label><input type=text id=ip placeholder="192.168.1.100"></div>
      <div><label class=lbl>Subnetzmaske</label><input type=text id=sm value="255.255.255.0"></div>
    </div>
    <div class=row2>
      <div><label class=lbl>Gateway</label><input type=text id=gw placeholder="192.168.1.1"></div>
      <div><label class=lbl>DNS-Server</label><input type=text id=dn placeholder="192.168.1.1"></div>
    </div>
  </div>
  <label class=lbl>Zeitserver (NTP)</label>
  <input type=text id=nt value="pool.ntp.org">
</div>
<div class=card>
  <button class="btn primary full" onclick=save()>Verbinden &amp; Speichern</button>
  <p class=msg>Das Ger&auml;t startet neu und verbindet sich mit dem WLAN.</p>
</div>
<script>
function save(){
  var b={ssid:s.value,pass:p.value,dhcp:d.checked,
    ip:document.getElementById('ip').value,mask:sm.value,gw:gw.value,dns:dn.value,ntp:nt.value};
  fetch('/api/config/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})
  .then(function(r){return r.json();}).then(function(r){alert(r.ok?'Gespeichert! Neustart...':'Fehler: '+(r.error||''));})
  .catch(function(){alert('Verbindungsfehler');});
}
</script></body></html>
)HTML";

// ─── Player-Seite ─────────────────────────────────────────────────────────────

static const char PAGE_MAIN[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang=de>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Squeezie</title>
<style>
)HTML";
static const char PAGE_MAIN2[] PROGMEM = R"HTML(
body{display:flex;flex-direction:column;min-height:100vh;max-width:560px;margin:0 auto}
#sb{background:#111;border-bottom:1px solid #2a2a2a;padding:10px 18px;
  display:flex;justify-content:space-between;align-items:center;flex-shrink:0}
#sb-t{color:#00c853;font-weight:700;font-size:1.1em;min-width:56px}
#sb-p{color:#b0b0b0;font-size:1em;overflow:hidden;text-overflow:ellipsis;
  white-space:nowrap;flex:1;text-align:center;padding:0 12px}
#notice{display:none;background:#2a1200;color:#ff9500;padding:12px 18px;
  font-size:1em;text-align:center;flex-shrink:0;border-bottom:1px solid #5a3000}
#content{flex:1;overflow-y:auto;padding:16px 18px 100px}
#cover-wrap{position:relative;width:min(100%,512px);aspect-ratio:1/1;
  margin:0 auto 20px;background-color:#1a1a1a;background-position:center center;
  background-size:cover;background-repeat:no-repeat;border-radius:14px;overflow:hidden;
  box-shadow:0 6px 24px rgba(0,0,0,.7)}
#ttl{font-size:1.5em;font-weight:700;color:#f0f0f0;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-bottom:8px}
#art{color:#00c853;font-size:1.2em;white-space:nowrap;overflow:hidden;
  text-overflow:ellipsis;margin-bottom:6px}
#alb{color:#a0a0a0;font-size:1.1em;white-space:nowrap;overflow:hidden;
  text-overflow:ellipsis;margin-bottom:4px}
#trk{color:#666;font-size:1em;margin-bottom:18px}
#pb-wrap{width:100%;height:8px;background:#2a2a2a;border-radius:4px;
  cursor:pointer;margin-bottom:8px}
#pb-fill{height:8px;background:#00c853;border-radius:4px;width:0;pointer-events:none}
#pb-time{display:flex;justify-content:space-between;font-size:1em;color:#707070;margin-bottom:24px}
#ctrl-transport{display:flex;justify-content:center;align-items:center;gap:20px;margin-bottom:20px}
#ctrl-volume{display:flex;align-items:center;gap:12px;margin-bottom:18px;padding:0 4px}
#vol-slider{flex:1;accent-color:#00c853;cursor:pointer;height:6px;outline:none}
#vol-slider:disabled{accent-color:#777;cursor:not-allowed;opacity:.55}
#vv{min-width:56px;text-align:right;font-size:1em;color:#d0d0d0;font-weight:600}
#ctrl-volume.vol-muted #vv{color:#888}
#ctrl-volume.vol-muted .cb:not(.mute){border-color:#666;color:#777;filter:grayscale(1);cursor:not-allowed;box-shadow:none}
#ctrl-modes{display:flex;gap:12px;margin-bottom:8px}
.cb{background:linear-gradient(180deg,#4a4a4a 0%,#2a2a2a 100%);border:2px solid #c8c8c8;color:#fff;border-radius:50%;
  width:64px;height:64px;display:flex;align-items:center;justify-content:center;
  cursor:pointer;flex-shrink:0;transition:transform .1s;box-shadow:0 8px 20px rgba(0,0,0,.35)}
.cb:active{transform:scale(.88);filter:brightness(.94)}
.cb svg{width:28px;height:28px;fill:currentColor;pointer-events:none}
.cb.play{background:linear-gradient(180deg,#34eb7d 0%,#00c853 100%);border-color:#eafff0;color:#001b08;width:84px;height:84px}
.cb.play svg{width:40px;height:40px}
.cb.play.paused{background:linear-gradient(180deg,#3a3a3a 0%,#242424 100%);border-color:#7cf0a7;color:#7cf0a7}
.cb.sm{width:52px;height:52px}
.cb.sm svg{width:24px;height:24px}
.cb.mute{border-color:#ffb0b0;color:#fff}
.mode-btn{flex:1;padding:14px 8px;background:#1e1e1e;border:2px solid #444;
  border-radius:12px;color:#a0a0a0;font-size:1em;cursor:pointer;
  text-align:center;line-height:1.4;transition:border-color .2s}
.mode-btn.on{background:#0a2e16;border-color:#00c853;color:#00c853}
.mode-btn:active{opacity:.6}
.tab-bar{background:#0d0d0d;border-top:1px solid #2a2a2a;
  position:fixed;bottom:0;left:0;right:0;display:flex;z-index:100;max-width:560px;margin:0 auto}
.tab-item{color:#888;font-size:1em}
</style></head><body>
<div id=sb>
  <span id=sb-t>--:--</span>
  <span id=sb-p>&mdash;</span>
  <a href=/settings style="color:#888;font-size:1em;letter-spacing:.5px">EINST.</a>
</div>
<div id=notice></div>
<div id=content>
  <div id=cover-wrap style="background-image:url('__COVER_URL__')"></div>
  <div id=ttl>&mdash;</div>
  <div id=art></div>
  <div id=alb></div>
  <div id=trk></div>
  <div id=pb-wrap onclick=seek(event)><div id=pb-fill></div></div>
  <div id=pb-time><span id=ps>0:00</span><span id=ds>0:00</span></div>
  <div id=ctrl-transport>
    <button class=cb onclick="c('prev')" title="Zur&uuml;ck">
      <svg viewBox="0 0 24 24"><path d="M6 6h2v12H6zm3.5 6 8.5 6V6z"/></svg>
    </button>
    <button class="cb play" id=bpl onclick="c('toggle')">
      <svg id=ico-play viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
      <svg id=ico-pause viewBox="0 0 24 24" style=display:none><path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/></svg>
    </button>
    <button class=cb onclick="c('next')" title="Weiter">
      <svg viewBox="0 0 24 24"><path d="M6 18l8.5-6L6 6v12zm2-8.14L11.03 12 8 14.14V9.86zM16 6h2v12h-2z"/></svg>
    </button>
  </div>
  <div id=ctrl-volume>
    <button class="cb sm" id=bvd onclick="c('vd')" title="Leiser">
      <svg viewBox="0 0 24 24"><path d="M14 3.23v2.06c2.89.86 5 3.54 5 6.71 0 .94-.2 1.82-.54 2.64l1.51 1.51A8.8 8.8 0 0 0 21 12c0-4.28-3-7.86-7-8.77zM3 9v6h4l5 5V4L7 9H3zM14 9.82v4.36c1.19-.69 2-1.97 2-3.18s-.81-2.49-2-3.18z"/></svg>
    </button>
    <input type=range id=vol-slider min=0 max=100 value=0
      oninput="pvol(this.value)" onchange="svol(this.value)">
    <span id=vv>0&nbsp;%</span>
    <button class="cb sm" id=bvu onclick="c('vu')" title="Lauter">
      <svg viewBox="0 0 24 24"><path d="M3 9v6h4l5 5V4L7 9H3zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4-.91 7-4.49 7-8.77s-3-7.86-7-8.77zM14 9.82v4.36c1.19-.69 2-1.97 2-3.18s-.81-2.49-2-3.18z"/></svg>
    </button>
    <button class="cb sm mute" id=bmut onclick="c('mut')" title="Stummschalten">
      <svg viewBox="0 0 24 24"><path d="M16.5 12A4.5 4.5 0 0 0 14 7.97v2.21l2.45 2.45c.03-.2.05-.41.05-.63zm2.5 0c0 .94-.2 1.82-.54 2.64l1.51 1.51A8.8 8.8 0 0 0 21 12c0-4.28-3-7.86-7-8.77v2.06c2.89.86 5 3.54 5 6.71zM4.27 3 3 4.27 7.73 9H3v6h4l5 5v-6.73l4.25 4.25c-.67.52-1.42.93-2.25 1.18v2.06A8.99 8.99 0 0 0 17.73 18L19 19.27 20.27 18 5.27 3 4.27 3zM12 4 9.91 6.09 12 8.18V4z"/></svg>
    </button>
  </div>
  <div id=ctrl-modes>
    <button class=mode-btn id=brp onclick="c('rpt')">
      <svg viewBox="0 0 24 24" style="width:22px;height:22px;vertical-align:middle;fill:currentColor"><path d="M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z"/></svg>
      &ensp;<span id=rpt-lbl>Wiederholen: aus</span>
    </button>
    <button class=mode-btn id=bsh onclick="c('shf')">
      <svg viewBox="0 0 24 24" style="width:22px;height:22px;vertical-align:middle;fill:currentColor"><path d="M10.59 9.17 5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z"/></svg>
      &ensp;<span id=shf-lbl>Zufall: aus</span>
    </button>
  </div>
</div>
<nav class=tab-bar>
  <a href=/ class="tab-item active">
    <svg viewBox="0 0 24 24"><path d="M12 3v10.55A4 4 0 1 0 14 17V7h4V3z"/></svg>
    Player
  </a>
  <a href=/settings class=tab-item>
    <svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94s-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.49.49 0 0 0-.59-.22l-2.39.96a7 7 0 0 0-1.62-.94l-.36-2.54A.484.484 0 0 0 14 2h-4c-.25 0-.46.18-.49.42l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96a.49.49 0 0 0-.59.22L2.74 8.87a.48.48 0 0 0 .12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.37 1.04.7 1.62.94l.36 2.54c.05.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.36-2.54c.59-.24 1.13-.57 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32a.48.48 0 0 0-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
    Einstellungen
  </a>
</nav>
<script>
var dur=0,prevCover='__COVER_URL__',volTimer=null,volMuted=false;
var RPT=['Wiederholen: aus','Wiederholen: Titel','Wiederholen: Playlist'];
var SHF=['Zufall: aus','Zufall: Titel','Zufall: Alben'];
function fmt(s){s=Math.round(s);return Math.floor(s/60)+':'+(s%60<10?'0':'')+s%60;}
function txt(id,val){var e=document.getElementById(id);if(e&&e.textContent!==val)e.textContent=val;}
function cls(id,val){var e=document.getElementById(id);if(e&&e.className!==val)e.className=val;}
function vis(id,show){var e=document.getElementById(id);if(e)e.style.display=show?'':'none';}
function dis(id,disabled){var e=document.getElementById(id);if(e)e.disabled=disabled;}
function upd(d){
  if(!d)return;
  var ntc=document.getElementById('notice');
  if(!d.connected){
    ntc.style.display='block';
    ntc.textContent='Kein WLAN \u2014 Server in den Einstellungen konfigurieren';
  } else if(!d.playerName){
    ntc.style.display='block';
    ntc.textContent='Kein Player ausgew\u00e4hlt \u2014 Einstellungen \u2192 Server';
  } else {
    ntc.style.display='none';
  }
  txt('ttl',d.title||'\u2014');
  txt('art',d.artist||'');
  txt('alb',d.album||'');
  txt('trk',d.trackNum?'Track '+d.trackNum:'');
  var playing=d.playing||false;
  vis('ico-play',!playing);
  vis('ico-pause',playing);
  cls('bpl','cb play'+(playing?'':' paused'));
  volMuted=d.muted||false;
  cls('ctrl-volume',''+(volMuted?'vol-muted':''));
  dis('vol-slider',volMuted);
  dis('bvd',volMuted);
  dis('bvu',volMuted);
  cls('bmut','cb sm mute'+(volMuted?' on':''));
  if(!volTimer){
    var v=d.volume||0;
    document.getElementById('vol-slider').value=v;
    txt('vv',v+'\u00a0%');
  }
  dur=d.duration||0;
  txt('ps',fmt(d.position||0));
  txt('ds',fmt(dur));
  var pct=dur>0?Math.min(100,Math.round((d.position||0)/dur*1000)/10):0;
  document.getElementById('pb-fill').style.width=pct+'%';
  var cover=d.coverUrl||'';
  if(cover!==prevCover){
    prevCover=cover;
    document.getElementById('cover-wrap').style.backgroundImage = cover ? 'url("'+cover+'")' : 'none';
  }
  cls('brp','mode-btn'+(d.repeatMode?' on':''));
  txt('rpt-lbl',RPT[d.repeatMode||0]);
  cls('bsh','mode-btn'+(d.shuffleMode?' on':''));
  txt('shf-lbl',SHF[d.shuffleMode||0]);
  txt('sb-p',d.playerName||'\u2014');
}
function poll(){
  fetch('/api/status').then(function(r){return r.json();}).then(upd).catch(function(){});
  var n=new Date();
  txt('sb-t',n.getHours().toString().padStart(2,'0')+':'+n.getMinutes().toString().padStart(2,'0'));
}
function c(cmd){
  fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({cmd:cmd})}).then(function(){setTimeout(poll,100);});
}
function pvol(v){
  if(volMuted)return;
  txt('vv',v+'\u00a0%');
  if(volTimer)clearTimeout(volTimer);
  volTimer=setTimeout(function(){volTimer=null;},1500);
}
function svol(v){
  if(volMuted)return;
  if(volTimer)clearTimeout(volTimer);
  volTimer=null;
  document.getElementById('vol-slider').value=v;
  txt('vv',v+'\u00a0%');
  c('vol:'+v);
}
function seek(e){
  if(!dur)return;
  var r=e.currentTarget.getBoundingClientRect();
  c('seek:'+(Math.round((e.clientX-r.left)/r.width*dur)));
}
setInterval(poll,200);poll();
</script></body></html>
)HTML";

// ─── Einstellungsseite ────────────────────────────────────────────────────────

static const char PAGE_SETTINGS[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang=de>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Squeezie &mdash; Einstellungen</title>
<style>
)HTML";
static const char PAGE_SETTINGS2[] PROGMEM = R"HTML(
body{max-width:560px;margin:0 auto;display:flex;flex-direction:column;min-height:100vh}
#page-hdr{background:#111;border-bottom:1px solid #222;padding:14px 18px;
  font-weight:700;font-size:1.1em;color:#e0e0e0;flex-shrink:0}
#itabs{display:flex;background:#161616;border-bottom:2px solid #2a2a2a;flex-shrink:0}
.itab{flex:1;padding:14px 4px;background:none;border:none;color:#888;
  font-size:1em;font-weight:600;cursor:pointer;letter-spacing:.3px;
  border-bottom:2px solid transparent;margin-bottom:-2px}
.itab.act{color:#00c853;border-bottom:2px solid #00c853}
.tc{display:none;flex:1;overflow-y:auto;padding:16px 16px 100px}
.tc.show{display:block}
#scan-res{font-size:1em;color:#00c853;margin-top:8px;min-height:1.4em}
#pl-list{list-style:none;margin:10px 0 0}
#pl-list li{padding:14px 16px;background:#1e1e1e;border:2px solid #333;
  border-radius:8px;margin-bottom:8px;cursor:pointer;
  display:flex;justify-content:space-between;align-items:center}
#pl-list li:active{opacity:.7}
#pl-list li.sel{background:#0a2e16;border-color:#00c853}
.pl-name{color:#e0e0e0;font-size:1em}
.pl-mac{color:#606060;font-size:1em}
#pls-list{list-style:none;margin:10px 0 0}
#pls-list li{padding:14px 16px;background:#1e1e1e;border:2px solid #333;
  border-radius:8px;margin-bottom:8px;cursor:pointer;
  display:flex;justify-content:space-between;align-items:center}
#pls-list li:active{opacity:.7}
#pls-list li.sel{background:#0a2e16;border-color:#00c853}
.pls-name{color:#e0e0e0;font-size:1em}
.pls-cnt{color:#606060;font-size:.9em}
#load-err{color:#e94560;font-size:1em;padding:12px 0;display:none}
</style></head><body>
<div id=page-hdr>Einstellungen</div>
<div id=itabs>
  <button class="itab act" onclick="tab('wlan',this)">WLAN&nbsp;/&nbsp;Netz</button>
  <button class=itab onclick="tab('srv',this)">Server</button>
  <button class=itab onclick="tab('sys',this)">System</button>
</div>

<!-- Tab: WLAN/Netz -->
<div id=tc-wlan class="tc show">
  <div id=load-err>Konfiguration konnte nicht geladen werden &mdash; Seite neu laden.</div>
  <div class=card>
    <h3>WLAN</h3>
    <label class=lbl>Netzwerk (SSID)</label><input type=text id=ss>
    <label class=lbl>Passwort (leer lassen = unver&auml;ndert)</label>
    <input type=password id=pw placeholder="(unver&auml;ndert)">
  </div>
  <div class=card>
    <h3>IP-Konfiguration</h3>
    <div class=chk>
      <input type=checkbox id=dh onchange="document.getElementById('sf').style.display=this.checked?'none':'block'">
      <span>DHCP &mdash; IP automatisch beziehen</span>
    </div>
    <div id=sf style=display:none>
      <div class=row2>
        <div><label class=lbl>IP-Adresse</label><input type=text id=ip></div>
        <div><label class=lbl>Subnetzmaske</label><input type=text id=mk></div>
      </div>
      <div class=row2>
        <div><label class=lbl>Gateway</label><input type=text id=gw></div>
        <div><label class=lbl>DNS-Server</label><input type=text id=dn></div>
      </div>
    </div>
    <label class=lbl>NTP-Zeitserver</label>
    <input type=text id=nt>
    <button class="btn primary full" onclick=svNet()>Netzwerk speichern &amp; Neustart</button>
  </div>
</div>

<!-- Tab: Server -->
<div id=tc-srv class=tc>
  <div class=card>
    <h3>Squeezebox / Lyrion Server</h3>
    <div class=row2>
      <div><label class=lbl>Server-Adresse&nbsp;/&nbsp;IP</label><input type=text id=sh placeholder="192.168.1.x"></div>
      <div><label class=lbl>Port</label><input type=number id=sp min=1 max=65535></div>
    </div>
    <div class=chk>
      <input type=checkbox id=sv>
      <span>Gruppenlautstärke für synchronisierte Player</span>
    </div>
    <div style="display:flex;gap:10px;margin-bottom:0">
      <button class=btn onclick=scan() id=scan-btn>Automatisch suchen</button>
      <button class="btn primary" onclick=svSrv() style="flex:1">Speichern</button>
    </div>
    <div id=scan-res></div>
  </div>
  <div class=card>
    <h3>Player-Auswahl</h3>
    <button class=btn onclick=ldPl()>Player laden</button>
    <ul id=pl-list></ul>
  </div>
  <div class=card>
    <h3>Playlist-Auswahl</h3>
    <div id=cur-pl class=msg>Keine Playlist ausgew&auml;hlt</div>
    <button class=btn onclick=ldPls() style="margin-top:10px">Playlists laden</button>
    <ul id=pls-list></ul>
  </div>
</div>

<!-- Tab: System -->
<div id=tc-sys class=tc>
  <div class=card>
    <h3>Anzeige</h3>
    <label class=lbl>Scrollgeschwindigkeit (Pixel / Sekunde)</label>
    <input type=number id=sc min=5 max=200>
    <button class="btn primary full" onclick=svSrv()>Speichern</button>
  </div>
  <div class=card>
    <h3>Firmware-Update (OTA)</h3>
    <form method=POST action=/update enctype=multipart/form-data
      onsubmit="document.getElementById('ota-msg').textContent='Wird hochgeladen \u2014 bitte warten...';">
      <input type=file name=firmware accept=.bin style="display:block;color:#9e9e9e;margin-bottom:12px;font-size:1em">
      <button type=submit class="btn primary full">Firmware hochladen</button>
    </form>
    <div id=ota-msg class=msg></div>
  </div>
  <div class=card>
    <h3>Ger&auml;te-Info</h3>
    <div id=dev-info class=msg>Wird geladen...</div>
  </div>
</div>

<nav class=tab-bar>
  <a href=/ class=tab-item>
    <svg viewBox="0 0 24 24"><path d="M12 3v10.55A4 4 0 1 0 14 17V7h4V3z"/></svg>
    Player
  </a>
  <a href=/settings class="tab-item active">
    <svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94s-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.49.49 0 0 0-.59-.22l-2.39.96a7 7 0 0 0-1.62-.94l-.36-2.54A.484.484 0 0 0 14 2h-4c-.25 0-.46.18-.49.42l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96a.49.49 0 0 0-.59.22L2.74 8.87a.48.48 0 0 0 .12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.37 1.04.7 1.62.94l.36 2.54c.05.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.36-2.54c.59-.24 1.13-.57 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32a.48.48 0 0 0-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
    Einstellungen
  </a>
</nav>
<script>
var mac='',curPlId='';
function tab(id,btn){
  document.querySelectorAll('.tc').forEach(function(e){e.className='tc';});
  document.querySelectorAll('.itab').forEach(function(e){e.className='itab';});
  document.getElementById('tc-'+id).className='tc show';
  btn.className='itab act';
}
function svNet(){
  var pw=document.getElementById('pw').value;
  var d={ssid:document.getElementById('ss').value,
    dhcp:document.getElementById('dh').checked,
    ip:document.getElementById('ip').value,mask:document.getElementById('mk').value,
    gw:document.getElementById('gw').value,dns:document.getElementById('dn').value,
    ntp:document.getElementById('nt').value};
  if(pw)d.pass=pw;
  fetch('/api/config/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})
  .then(function(r){return r.json();})
  .then(function(r){alert(r.ok?'Gespeichert! Das Ger\u00e4t startet neu.':'Fehler: '+(r.error||''));})
  .catch(function(e){alert('Verbindungsfehler: '+e);});
}
function svSrv(){
  var d={srvHost:document.getElementById('sh').value,
    srvPort:parseInt(document.getElementById('sp').value)||9000,
    scrollSpeed:parseInt(document.getElementById('sc').value)||30,
    playerMac:mac,
    syncVolume:document.getElementById('sv').checked};
  fetch('/api/config/server',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})
  .then(function(r){return r.json();})
  .then(function(r){alert(r.ok?'Gespeichert!':'Fehler: '+(r.error||''));})
  .catch(function(e){alert('Verbindungsfehler: '+e);});
}
function scan(){
  var btn=document.getElementById('scan-btn');
  btn.disabled=true;btn.textContent='Suche l\u00e4uft...';
  document.getElementById('scan-res').textContent='';
  fetch('/api/scan',{method:'POST'})
  .then(function(r){return r.json();})
  .then(function(d){
    btn.disabled=false;btn.textContent='Automatisch suchen';
    if(d.host){
      document.getElementById('sh').value=d.host;
      document.getElementById('sp').value=d.port||9000;
      document.getElementById('scan-res').textContent='\u2714 Gefunden: '+d.host+':'+d.port;
    } else {
      document.getElementById('scan-res').style.color='#e94560';
      document.getElementById('scan-res').textContent='\u2717 Kein Server gefunden \u2014 IP manuell eingeben.';
    }
  })
  .catch(function(){
    btn.disabled=false;btn.textContent='Automatisch suchen';
    document.getElementById('scan-res').textContent='Verbindungsfehler';
  });
}
function ldPl(){
  fetch('/api/players').then(function(r){return r.json();}).then(function(arr){
    var ul=document.getElementById('pl-list');ul.innerHTML='';
    if(!arr||!arr.length){
      ul.innerHTML='<li style="color:#808080;border:none;background:none">Keine Player \u2014 Server konfiguriert?</li>';return;}
    arr.forEach(function(p){
      var li=document.createElement('li');
      if(p.mac===mac)li.className='sel';
      li.innerHTML='<span class=pl-name>'+p.name+'</span><span class=pl-mac>'+p.mac+'</span>';
      li.onclick=function(){
        mac=p.mac;
        ul.querySelectorAll('li').forEach(function(l){l.className='';});
        li.className='sel';
        fetch('/api/config/player',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mac:p.mac})});
      };
      ul.appendChild(li);
    });
  }).catch(function(){
    document.getElementById('pl-list').innerHTML='<li style="color:#e94560;border:none;background:none">Fehler beim Laden der Player</li>';
  });
}
function ldPls(){
  fetch('/api/playlists').then(function(r){return r.json();}).then(function(arr){
    var ul=document.getElementById('pls-list');ul.innerHTML='';
    if(!arr||!arr.length){
      ul.innerHTML='<li style="color:#808080;border:none;background:none">Keine Playlists \u2014 Server konfiguriert?</li>';return;}
    arr.forEach(function(p){
      var li=document.createElement('li');
      if(p.id===curPlId)li.className='sel';
      li.innerHTML='<span class=pls-name>'+p.name+'</span>'+(p.trackCount>0?'<span class=pls-cnt>'+p.trackCount+' Titel</span>':'');
      li.onclick=function(){
        curPlId=p.id;
        ul.querySelectorAll('li').forEach(function(l){l.className='';});
        li.className='sel';
        document.getElementById('cur-pl').textContent=p.name;
        fetch('/api/config/playlist',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:p.id,name:p.name})});
      };
      ul.appendChild(li);
    });
  }).catch(function(){
    document.getElementById('pls-list').innerHTML='<li style="color:#e94560;border:none;background:none">Fehler beim Laden</li>';
  });
}
fetch('/api/config').then(function(r){return r.json();}).then(function(d){
  document.getElementById('ss').value=d.ssid||'';
  document.getElementById('dh').checked=d.dhcp!==false;
  document.getElementById('ip').value=d.ip||'';document.getElementById('mk').value=d.mask||'255.255.255.0';
  document.getElementById('gw').value=d.gw||'';document.getElementById('dn').value=d.dns||'';
  document.getElementById('nt').value=d.ntp||'pool.ntp.org';
  document.getElementById('sh').value=d.srvHost||'';document.getElementById('sp').value=d.srvPort||9000;
  document.getElementById('sc').value=d.scrollSpeed||30;
  document.getElementById('sv').checked=d.syncVolume!==false;
  mac=d.playerMac||'';
  curPlId=d.playlistId||'';
  if(d.playlistName)document.getElementById('cur-pl').textContent=d.playlistName;
  document.getElementById('dh').dispatchEvent(new Event('change'));
  var ip=d.deviceIp||d.ip||'\u2014';
  document.getElementById('dev-info').innerHTML=
    'IP: <b>'+ip+'</b> &nbsp; Server: <b>'+(d.srvHost||'\u2014')+'</b><br>Player: <b>'+(d.playerMac||'\u2014')+'</b>';
}).catch(function(e){
  document.getElementById('load-err').style.display='block';
  document.getElementById('dev-info').textContent='Fehler: '+e;
});
</script></body></html>
)HTML";

// ─── API-Hilfsfunktion ────────────────────────────────────────────────────────

static int interpolatePosition(const SqueezieStatus& s) {
    static int      lastPos  = -1;
    static uint32_t lastMs   = 0;
    if (s.playing) {
        if (s.position != lastPos) {
            lastPos = s.position;
            lastMs  = millis();
        }
        int elapsed = (int)((millis() - lastMs) / 1000);
        return lastPos + elapsed;
    }
    lastPos = s.position;
    lastMs  = millis();
    return s.position;
}

static String browserCoverSourceUrl() {
    if (g_srv.host.isEmpty() || g_srv.playerMac.isEmpty()) return "";
    return "http://" + g_srv.host + ":" + String(g_srv.port)
         + "/music/current/cover_512x512_o.jpg?player=" + g_srv.playerMac;
}

static bool webCoverTaskTimedOut(uint32_t now, uint32_t startedAt) {
    return (now - startedAt) > WEB_COVER_TASK_TIMEOUT_MS;
}

static void webCoverTask(void* p) {
    (void)p;
    xSemaphoreTake(g_webCoverMux, portMAX_DELAY);
    String url = g_webPendingCoverUrl;
    String key = g_webPendingCoverKey;
    g_webPendingCoverUrl    = "";
    g_webPendingCoverKey    = "";
    g_webCoverTaskStartedAt = millis();
    xSemaphoreGive(g_webCoverMux);

    bool   ok  = false;
    size_t len = 0;

    if (!url.isEmpty() && g_webCoverBuf) {
        HTTPClient http;
        http.setTimeout(3000);
        if (http.begin(url)) {
            int code = http.GET();
            if (code == 200) {
                WiFiClient* stream = http.getStreamPtr();
                if (stream) {
                    uint8_t buf[2048];
                    uint32_t last = millis();
                    while (http.connected() && stream->connected() && len < WEB_COVER_BUF_SIZE) {
                        int avail = stream->available();
                        if (avail <= 0) {
                            if (millis() - last > 1200) break;
                            delay(1);
                            continue;
                        }
                        last = millis();
                        int room = WEB_COVER_BUF_SIZE - len;
                        int rd = stream->readBytes(buf, (avail < (int)sizeof(buf)) ? avail : (int)sizeof(buf));
                        if (rd <= 0) break;
                        if (rd > room) rd = room;
                        memcpy(g_webCoverBuf + len, buf, rd);
                        len += rd;
                    }
                    if (len > 1 && g_webCoverBuf[len - 2] == 0xFF && g_webCoverBuf[len - 1] == 0xD9) {
                        ok = true;
                    }
                }
            }
            http.end();
        }
    }

    xSemaphoreTake(g_webCoverMux, portMAX_DELAY);
    if (ok) {
        g_webCoverLen       = len;
        g_webLoadedCoverKey = key;
        g_webCoverSerial++;
        g_webCoverRetryAt   = 0;
    } else {
        g_webCoverRetryAt   = millis() + WEB_COVER_RETRY_DELAY_MS;
    }
    g_webCoverTask          = nullptr;
    g_webCoverTaskStartedAt  = 0;
    xSemaphoreGive(g_webCoverMux);

    vTaskDelete(nullptr);
}

static void webCoverPreload(const String& url, const String& key) {
    if (url.isEmpty() || key.isEmpty()) return;

    xSemaphoreTake(g_webCoverMux, portMAX_DELAY);

    if (g_webCoverLen > 0 && g_webLoadedCoverKey == key) {
        xSemaphoreGive(g_webCoverMux);
        return;
    }
    if (g_webCoverTask != nullptr) {
        if (webCoverTaskTimedOut(millis(), g_webCoverTaskStartedAt)) {
            TaskHandle_t staleTask = g_webCoverTask;
            g_webCoverTask = nullptr;
            vTaskDelete(staleTask);
        } else {
            g_webPendingCoverUrl = url;
            g_webPendingCoverKey = key;
            xSemaphoreGive(g_webCoverMux);
            return;
        }
    }
    if (g_webCoverRetryAt != 0 && millis() < g_webCoverRetryAt) {
        xSemaphoreGive(g_webCoverMux);
        return;
    }

    g_webPendingCoverUrl    = url;
    g_webPendingCoverKey    = key;
    g_webCoverTaskStartedAt  = millis();
    g_webCoverRetryAt        = 0;
    BaseType_t created = xTaskCreatePinnedToCore(webCoverTask, "webCover", 8192, nullptr, 1, &g_webCoverTask, 0);
    if (created != pdPASS) {
        g_webCoverTask       = nullptr;
        g_webCoverRetryAt    = millis() + WEB_COVER_RETRY_DELAY_MS;
        g_webPendingCoverUrl = "";
        g_webPendingCoverKey = "";
        xSemaphoreGive(g_webCoverMux);
        return;
    }
    xSemaphoreGive(g_webCoverMux);
}

static String browserCoverUrl(uint32_t coverV) {
    (void)coverV;
    return (g_webCoverLen == 0 || g_webLoadedCoverKey.isEmpty())
        ? ""
        : (g_srv.host.isEmpty() || g_srv.playerMac.isEmpty())
        ? ""
        : "/api/cover-large?v=" + String(g_webCoverSerial);
}

static String statusToJson() {
    xSemaphoreTake(g_statusMux, portMAX_DELAY);
    SqueezieStatus s = g_status;
    xSemaphoreGive(g_statusMux);
    uint32_t coverV = displayGetCoverSerial();
    int pos = interpolatePosition(s);
    JsonDocument doc;
    doc["playing"]    = s.playing;
    doc["title"]      = s.title;
    doc["artist"]     = s.artist;
    doc["album"]      = s.album;
    doc["trackNum"]   = s.trackNum;
    doc["duration"]   = s.duration;
    doc["position"]   = pos;
    doc["volume"]     = s.volume;
    doc["muted"]      = s.muted;
    doc["repeatMode"] = s.repeatMode;
    doc["shuffleMode"]= s.shuffleMode;
    doc["playerName"] = s.playerName;
    doc["coverUrl"]   = browserCoverUrl(coverV);
    doc["connected"]  = (g_wifiState == WIFI_STATE_CONNECTED);
    String out; serializeJson(doc, out);
    return out;
}

void wifiUpdateCachedStatus(const SqueezieStatus& status) {
    if (!g_statusMux) return;
    xSemaphoreTake(g_statusMux, portMAX_DELAY);
    g_status = status;
    xSemaphoreGive(g_statusMux);
}

static void addUniqueTarget(std::vector<String>& out, const String& mac) {
    if (mac.isEmpty()) return;
    for (const String& existing : out) {
        if (existing == mac) return;
    }
    out.push_back(mac);
}

static std::vector<String> volumeTargetsForPlayer(const String& mac) {
    std::vector<String> targets;
    if (mac.isEmpty()) return targets;
    if (g_srv.syncVolume) {
        squeezeGetSyncGroupMembers(mac, targets);
    }
    addUniqueTarget(targets, mac);
    return targets;
}

static void reconcileSyncVolume() {
    if (!g_srv.syncVolume || g_srv.playerMac.isEmpty()) return;
    uint32_t now = millis();
    if (now < g_fastPollUntil) return;
    if (g_lastSyncReconcile == 0) {
        g_lastSyncReconcile = now;
        return;
    }
    if (now - g_lastSyncReconcile < SYNC_RECONCILE_INTERVAL_MS) return;
    g_lastSyncReconcile = now;

    SqueezieStatus status = squeezieGetCachedStatus();
    std::vector<String> targets = volumeTargetsForPlayer(g_srv.playerMac);
    if (targets.size() < 2) return;

    bool mismatch = false;
    for (const String& target : targets) {
        int vol = -1;
        bool muted = false;
        if (!squeezeGetVolumeState(target, vol, muted)) continue;
        if (vol != status.volume || muted != status.muted) {
            mismatch = true;
            break;
        }
    }
    if (!mismatch) return;

    if (status.muted) {
        for (const String& target : targets) {
            squeezeSetMute(target, true);
        }
    } else {
        for (const String& target : targets) {
            squeezeSetVolume(target, status.volume);
        }
    }
}

bool wifiPollNow() {
    if (g_wifiState != WIFI_STATE_CONNECTED || g_srv.playerMac.isEmpty() || g_otaActive) return false;
    SqueezieStatus tmp;
    if (!squeezeGetStatus(g_srv.playerMac, tmp)) {
        if (g_lmsFailStreak < 255) g_lmsFailStreak++;
        srvLog("[LMS] Sofort-Poll fehlgeschlagen.");
        return false;
    }
    g_lastLmsOkAt = millis();
    g_lmsFailStreak = 0;
    xSemaphoreTake(g_statusMux, portMAX_DELAY);
    g_status = tmp;
    xSemaphoreGive(g_statusMux);
    if (tmp.coverKey != g_webLoadedCoverKey) {
        String webUrl = browserCoverSourceUrl();
        webCoverPreload(webUrl, tmp.coverKey);
    }
    displayPreloadCover(tmp.coverUrl, tmp.coverKey);
    return true;
}

static void scheduleRestart(uint32_t delayMs = 500) {
    g_restartAt = millis() + delayMs;
}

bool wifiConsumeOtaProgress(int& pct) {
    int currentPct = g_otaUiPct;
    bool dirty = g_otaUiDirty;
    if (currentPct < 0) return false;
    pct = currentPct;
    if (dirty) g_otaUiDirty = false;
    return dirty;
}

bool wifiIsOtaActive() {
    return g_otaActive || g_otaUiPct >= 0;
}

// Mehrteilige Seite zusammensetzen und mit Cache-Control senden
static void sendPage(AsyncWebServerRequest* req,
                     const char* part1, const char* css,
                     const char* part2, const char* part3,
                     const String& coverUrl = "") {
    String page = String(FPSTR(part1)) + String(FPSTR(css)) + String(FPSTR(part2));
    if (part3) page += String(FPSTR(part3));
    if (!coverUrl.isEmpty()) page.replace("__COVER_URL__", coverUrl);
    else page.replace("__COVER_URL__", "");
    AsyncWebServerResponse* res = req->beginResponse(200, "text/html; charset=UTF-8", page);
    res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    res->addHeader("Pragma", "no-cache");
    req->send(res);
}

// ─── Routen registrieren ─────────────────────────────────────────────────────

static void setupRoutes() {
    server.on("/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        String out; out.reserve(LOG_LINES * LOG_LINE_LEN);
        for (int i = 0; i < LOG_LINES; i++) {
            int idx = (s_logHead + i) % LOG_LINES;
            char line[LOG_LINE_LEN];
            portENTER_CRITICAL(&s_logMux);
            memcpy(line, s_log[idx], LOG_LINE_LEN);
            portEXIT_CRITICAL(&s_logMux);
            if (line[0]) { out += line; out += '\n'; }
        }
        req->send(200, "text/plain; charset=utf-8", out);
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (g_wifiState == WIFI_STATE_AP)
            sendPage(req, PAGE_AP, CSS_COMMON, PAGE_AP2, nullptr);
        else
        {
            sendPage(req, PAGE_MAIN, CSS_COMMON, PAGE_MAIN2, nullptr,
                     browserCoverUrl(displayGetCoverSerial()));
        }
    });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendPage(req, PAGE_SETTINGS, CSS_COMMON, PAGE_SETTINGS2, nullptr);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", statusToJson());
    });

    server.on("/api/cover", HTTP_GET, [](AsyncWebServerRequest* req) {
        std::vector<uint8_t> cover;
        if (!displayCopyCover(cover)) {
            req->send(204, "image/jpeg", "");
            return;
        }
        AsyncResponseStream* res = req->beginResponseStream("image/jpeg");
        res->addHeader("Cache-Control", "no-store");
        res->write(cover.data(), cover.size());
        req->send(res);
    });

    server.on("/api/cover-large", HTTP_GET, [](AsyncWebServerRequest* req) {
        xSemaphoreTake(g_webCoverMux, portMAX_DELAY);
        bool ok = (g_webCoverBuf && g_webCoverLen > 0);
        if (!ok) {
            xSemaphoreGive(g_webCoverMux);
            req->send(204, "image/jpeg", "");
            return;
        }
        AsyncWebServerResponse* res = req->beginResponse(200, "image/jpeg", g_webCoverBuf, g_webCoverLen);
        res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        res->addHeader("Pragma", "no-cache");
        req->send(res);
        xSemaphoreGive(g_webCoverMux);
    });

    server.on("/api/control", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index + len < total) return;
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            String cmd = doc["cmd"].as<String>();
            String mac = g_srv.playerMac;
            SqueezieStatus status = squeezieGetCachedStatus();
            if (mac.isEmpty()) {
                req->send(200, "application/json", "{\"ok\":false,\"error\":\"kein Player\"}"); return;
            }
            bool ok = false;
            SqueezieStatus updated = status;
            wifiBeginUserCommand();
            if      (cmd == "toggle") {
                ok = status.playing ? squeezePause(mac) : squeezePlay(mac);
                if (ok) updated.playing = !status.playing;
            }
            else if (cmd == "next")   ok = squeezeNext(mac);
            else if (cmd == "prev")   ok = squeezePrev(mac);
            else if (cmd == "vu") {
                if (status.muted) {
                    ok = true;
                } else {
                    int nextVolume = constrain(status.volume + 1, 0, 100);
                    std::vector<String> targets = volumeTargetsForPlayer(mac);
                    ok = false;
                    for (const String& target : targets) {
                        if (squeezeSetVolume(target, nextVolume)) ok = true;
                    }
                    if (ok) {
                        updated.volume = nextVolume;
                        updated.muted = false;
                    }
                }
            }
            else if (cmd == "vd") {
                if (status.muted) {
                    ok = true;
                } else {
                    int nextVolume = constrain(status.volume - 1, 0, 100);
                    std::vector<String> targets = volumeTargetsForPlayer(mac);
                    ok = false;
                    for (const String& target : targets) {
                        if (squeezeSetVolume(target, nextVolume)) ok = true;
                    }
                    if (ok) {
                        updated.volume = nextVolume;
                        updated.muted = false;
                    }
                }
            }
            else if (cmd == "mut") {
                bool nextMute = !status.muted;
                std::vector<String> targets = volumeTargetsForPlayer(mac);
                ok = false;
                for (const String& target : targets) {
                    if (squeezeSetMute(target, nextMute)) ok = true;
                }
                if (ok) updated.muted = nextMute && updated.volume > 0;
            }
            else if (cmd.startsWith("vol:")) {
                if (status.muted) {
                    ok = true;
                } else {
                    int v = constrain(cmd.substring(4).toInt(), 0, 100);
                    std::vector<String> targets = volumeTargetsForPlayer(mac);
                    ok = false;
                    for (const String& target : targets) {
                        if (squeezeSetVolume(target, v)) ok = true;
                    }
                    if (ok) {
                        updated.volume = v;
                        updated.muted = false;
                    }
                }
            }
            else if (cmd == "rpt") {
                int nextRepeat = (status.repeatMode + 1) % 3;
                ok = squeezeSetRepeat(mac, nextRepeat);
                if (ok) updated.repeatMode = nextRepeat;
            }
            else if (cmd == "shf") {
                int nextShuffle = (status.shuffleMode + 1) % 3;
                ok = squeezeSetShuffle(mac, nextShuffle);
                if (ok) updated.shuffleMode = nextShuffle;
            }
            else if (cmd.startsWith("seek:")) {
                int seekPos = cmd.substring(5).toInt();
                ok = squeezeSeek(mac, seekPos);
                if (ok) updated.position = seekPos;
            }
            else {
                wifiEndUserCommand(false);
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"unbekannter Befehl\"}");
                return;
            }
            if (!ok) {
                wifiEndUserCommand(false);
                req->send(502, "application/json", "{\"ok\":false,\"error\":\"LMS-Befehl fehlgeschlagen\"}");
                return;
            }
            if (cmd != "next" && cmd != "prev") wifiUpdateCachedStatus(updated);
            wifiEndUserCommand(true);
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    server.on("/api/players", HTTP_GET, [](AsyncWebServerRequest* req) {
        std::vector<SqueeziePlayer> players;
        if (!squeezeGetPlayers(players)) { req->send(200, "application/json", "[]"); return; }
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (auto& p : players) {
            JsonObject o = arr.add<JsonObject>();
            o["mac"] = p.mac; o["name"] = p.name; o["connected"] = p.connected;
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/playlists", HTTP_GET, [](AsyncWebServerRequest* req) {
        std::vector<SqueeziePlaylist> lists;
        if (!squeezeGetPlaylists(lists)) { req->send(200, "application/json", "[]"); return; }
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (auto& pl : lists) {
            JsonObject o = arr.add<JsonObject>();
            o["id"]         = pl.id;
            o["name"]       = pl.name;
            o["trackCount"] = pl.trackCount;
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/scan", HTTP_POST, [](AsyncWebServerRequest* req) {
        String foundHost;
        uint16_t foundPort = LYRION_DEFAULT_PORT;
        if (squeezeDiscover(foundHost, foundPort, 8000)) {
            req->send(200, "application/json",
                "{\"host\":\"" + foundHost + "\",\"port\":" + String(foundPort) + "}");
        } else {
            req->send(200, "application/json", "{\"host\":null}");
        }
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["ssid"]        = g_net.ssid;  doc["dhcp"]        = g_net.dhcp;
        doc["ip"]          = g_net.ip;    doc["mask"]         = g_net.mask;
        doc["gw"]          = g_net.gw;    doc["dns"]          = g_net.dns;
        doc["ntp"]         = g_net.ntp;
        doc["srvHost"]     = g_srv.host;  doc["srvPort"]      = g_srv.port;
        doc["playerMac"]   = g_srv.playerMac; doc["scrollSpeed"] = g_srv.scrollSpeed;
        doc["syncVolume"]  = g_srv.syncVolume;
        doc["playlistId"]  = g_playlistId;   doc["playlistName"] = g_playlistName;
        doc["deviceIp"]    = wifiGetIP();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/config/network", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index + len < total) return;
            JsonDocument doc;
            if (deserializeJson(doc, (char*)data, len)) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"JSON\"}"); return;
            }
            String ssid = doc["ssid"] | "";
            if (ssid.isEmpty()) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"SSID fehlt\"}"); return;
            }
            g_net.ssid = ssid;
            if (doc["pass"].as<String>().length() > 0) g_net.pass = doc["pass"].as<String>();
            g_net.dhcp = doc["dhcp"] | true;
            g_net.ip   = doc["ip"]   | ""; g_net.mask = doc["mask"] | "255.255.255.0";
            g_net.gw   = doc["gw"]   | ""; g_net.dns  = doc["dns"]  | "";
            g_net.ntp  = doc["ntp"]  | "pool.ntp.org";
            saveNetCfg();
            req->send(200, "application/json", "{\"ok\":true}");
            scheduleRestart();
        }
    );

    server.on("/api/config/server", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index + len < total) return;
            JsonDocument doc;
            if (!deserializeJson(doc, (char*)data, len)) {
                if (doc["srvHost"].as<String>().length() > 0) g_srv.host = doc["srvHost"].as<String>();
                g_srv.port        = doc["srvPort"]     | (int)g_srv.port;
                g_srv.scrollSpeed = doc["scrollSpeed"] | g_srv.scrollSpeed;
                g_srv.syncVolume  = doc["syncVolume"]  | g_srv.syncVolume;
                if (doc["playerMac"].as<String>().length() > 0)
                    g_srv.playerMac = doc["playerMac"].as<String>();
                saveSrvCfg();
                squeezeSetServer(g_srv.host, g_srv.port);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    server.on("/api/config/player", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index + len < total) return;
            JsonDocument doc;
            if (!deserializeJson(doc, (char*)data, len)) {
                g_srv.playerMac = doc["mac"] | "";
                saveSrvCfg();
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    server.on("/api/config/playlist", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index + len < total) return;
            JsonDocument doc;
            if (!deserializeJson(doc, (char*)data, len)) {
                String id   = doc["id"]   | "";
                String name = doc["name"] | "";
                if (!id.isEmpty()) wifiSetPlaylist(id, name, true);
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            String body = ok
                ? "<html><body style='background:#0d0d0d;color:#00c853;font-family:sans-serif;"
                  "padding:30px;font-size:20px'><h2>Update erfolgreich!</h2><p>Ger&auml;t startet neu...</p></body></html>"
                : "<html><body style='background:#0d0d0d;color:#e94560;font-family:sans-serif;"
                  "padding:30px;font-size:20px'><h2>Update fehlgeschlagen!</h2><p>Fehlercode: "
                  + String(g_otaError) + "</p>"
                  "<a href='/settings' style='color:#aaa'>Zur&uuml;ck</a></body></html>";
            AsyncWebServerResponse* res = req->beginResponse(200, "text/html; charset=UTF-8", body);
            res->addHeader("Connection", "close");
            req->send(res);
            if (ok) scheduleRestart(1200);
            else g_otaActive = false;
        },
        [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                g_otaActive  = true;
                g_lastOtaPct = -1;
                g_otaUiPct   = 0;
                g_otaUiDirty = true;
                g_otaError   = 0;
                g_otaRejected = false;
                srvLog("[OTA] %s", filename.c_str());
                displayShowOTA(0);
                if (len == 0 || data[0] != 0xE9) {
                    g_otaError = UPDATE_ERROR_MAGIC_BYTE;
                    g_otaActive = false;
                    g_otaUiPct = -1;
                    g_otaRejected = true;
                    srvLog("[OTA] Ungültiges Firmware-Image: Magic=0x%02x",
                           len > 0 ? data[0] : 0);
                    return;
                }
                size_t otaSize = req->contentLength() > 0 ? req->contentLength() : UPDATE_SIZE_UNKNOWN;
                if (!Update.begin(otaSize, U_FLASH)) {
                    g_otaError = Update.getError();
                    g_otaActive = false;
                    g_otaUiPct = -1;
                    srvLog("[OTA] Begin fehlgeschlagen: err=%d", g_otaError);
                }
            }
            if (!g_otaRejected && !Update.hasError()) {
                size_t written = Update.write(data, len);
                if (written != len) {
                    g_otaError = Update.getError();
                    g_otaActive = false;
                    g_otaUiPct = -1;
                    srvLog("[OTA] Write fehlgeschlagen: %u/%u err=%d",
                           (unsigned)written, (unsigned)len, g_otaError);
                }
                if (req->contentLength() > 0) {
                    int pct = (int)((index + len) * 100 / req->contentLength());
                    if (pct != g_lastOtaPct) {
                        g_lastOtaPct = pct;
                        g_otaUiPct = pct;
                        g_otaUiDirty = true;
                        displayShowOTA(pct);
                    }
                }
            }
            if (final) {
                if (Update.end(true)) {
                    srvLog("[OTA] Fertig: %u Bytes", index + len);
                    g_otaActive = false;
                    g_otaUiPct = 100;
                    g_otaUiDirty = true;
                    displayShowOTA(100);
                } else {
                    g_otaError = Update.getError();
                    g_otaActive = false;
                    g_otaUiPct = -1;
                    g_otaUiDirty = true;
                    srvLog("[OTA] Fehler beim Abschluss: err=%d", g_otaError);
                }
            }
        }
    );

    server.onNotFound([](AsyncWebServerRequest* req) {
        if (g_wifiState == WIFI_STATE_AP) req->redirect("http://192.168.4.1/");
        else req->send(404, "text/plain", "Not found");
    });
}

// ─── Öffentliche API ─────────────────────────────────────────────────────────

void webserverInit() {
    g_statusMux = xSemaphoreCreateMutex();
    g_webCoverMux = xSemaphoreCreateMutex();
    g_webCoverBuf = (uint8_t*)ps_malloc(WEB_COVER_BUF_SIZE);
    loadCfg();
    WiFi.persistent(false);
    srvLog("[WebServer] SSID='%s'  Server='%s'",
           g_net.ssid.c_str(), g_srv.host.c_str());
    if (!connectWiFi()) {
        srvLog("[WiFi] Zweiter STA-Startversuch...");
        delay(150);
        if (!connectWiFi(4000)) startAP();
    }
    if (g_wifiState == WIFI_STATE_CONNECTED) {
        ArduinoOTA.setHostname(OTA_HOSTNAME);
        ArduinoOTA.setPassword(OTA_PASSWORD);
        ArduinoOTA.onStart([]() {
            g_otaActive = true;
            g_lastOtaPct = -1;
            g_otaUiPct = 0;
            g_otaUiDirty = true;
            g_otaError = 0;
            pauseLoopWatchdogForOta();
            displayShowOTA(0);
            srvLog("[OTA] ArduinoOTA gestartet.");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            g_otaActive = true;
            if (total > 0) {
                int pct = (int)(progress * 100 / total);
                if (pct != g_lastOtaPct) {
                    g_lastOtaPct = pct;
                    g_otaUiPct = pct;
                    g_otaUiDirty = true;
                    displayShowOTA(pct);
                }
            }
        });
        ArduinoOTA.onEnd([]() {
            g_otaUiPct = 100;
            g_otaUiDirty = true;
            g_otaActive = false;
            g_lastOtaPct = -1;
            displayShowOTA(100);
        });
        ArduinoOTA.onError([](ota_error_t error) {
            g_otaError = (int)error;
            g_otaUiPct = -1;
            g_otaUiDirty = true;
            g_otaActive = false;
            resumeLoopWatchdogAfterOta();
            srvLog("[OTA] ArduinoOTA-Fehler: err=%d", g_otaError);
        });
        ArduinoOTA.begin();
        srvLog("[OTA] ArduinoOTA bereit.");
    }
    setupRoutes();
    server.begin();
    xTaskCreatePinnedToCore([](void*) {
        uint32_t nextPollAt = 0;
        for (;;) {
            uint32_t now = millis();
            bool forced = g_forceRepoll;
            if (forced) g_forceRepoll = false;

            if (g_wifiState == WIFI_STATE_CONNECTED
                && !g_srv.playerMac.isEmpty()
                && !g_otaActive
                && !g_userCommandActive
                && (forced || now >= nextPollAt)) {
                if (wifiPollNow()) {
                    reconcileSyncVolume();
                } else {
                    srvLog("[LMS] Poll fehlgeschlagen.");
                }
                uint32_t interval = millis() < g_fastPollUntil
                    ? LMS_FAST_POLL_INTERVAL_MS
                    : LMS_POLL_INTERVAL_MS;
                nextPollAt = millis() + interval;
            }
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }, "lmsPoll", 8192, nullptr, 1, nullptr, 0);
    srvLog("[WebServer] gestartet.");
}

void webserverLoop() {
    if (g_wifiState == WIFI_STATE_AP) { dnsServer.processNextRequest(); }
    if (g_restartAt != 0 && millis() >= g_restartAt) {
        delay(100);
        ESP.restart();
    }
    if (g_wifiState == WIFI_STATE_AP) return;
    ArduinoOTA.handle();
    if (g_wifiState == WIFI_STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
        srvLog("[WiFi] Verbindung verloren.");
        g_wifiState   = WIFI_STATE_DISCONNECTED;
        g_reconnectAt = millis() + 30000;
    }
    if (g_wifiState == WIFI_STATE_CONNECTED
        && !g_otaActive
        && !g_srv.host.isEmpty()
        && !g_srv.playerMac.isEmpty()
        && millis() - g_wifiConnectedAt > 10000
        && millis() - g_lastLmsOkAt > 10000
        && g_lmsFailStreak >= 5) {
        srvLog("[WiFi] Verbunden, aber kein LMS-Verkehr (%u Fehler) - verbinde WLAN neu.",
               (unsigned)g_lmsFailStreak);
        WiFi.disconnect(true, false);
        g_wifiState   = WIFI_STATE_DISCONNECTED;
        g_reconnectAt = millis() + 500;
        g_lmsFailStreak = 0;
    }
    if (g_wifiState == WIFI_STATE_DISCONNECTED && millis() > g_reconnectAt) {
        if (!connectWiFi()) g_reconnectAt = millis() + 30000;
    }
}

WifiState wifiGetState()  { return g_wifiState; }
String    wifiGetIP()     {
    if (g_wifiState == WIFI_STATE_CONNECTED) return WiFi.localIP().toString();
    if (g_wifiState == WIFI_STATE_AP)        return WiFi.softAPIP().toString();
    return "";
}
SqueezieStatus squeezieGetCachedStatus() {
    if (!g_statusMux) return g_status;
    xSemaphoreTake(g_statusMux, portMAX_DELAY);
    SqueezieStatus snap = g_status;
    xSemaphoreGive(g_statusMux);
    return snap;
}
String                wifiGetPlayerMac()         { return g_srv.playerMac; }
int                   wifiGetScrollSpeed()        { return g_srv.scrollSpeed; }
bool                  wifiGetSyncVolume()         { return g_srv.syncVolume; }

void wifiSetServer(const String& host, uint16_t port) {
    g_srv.host = host;
    g_srv.port = port;
    squeezeSetServer(host, port);
    saveSrvCfg();
    g_forceRepoll = true;
    srvLog("[Settings] Server: %s:%d", host.c_str(), port);
}
void wifiSetPlayer(const String& mac) {
    g_srv.playerMac = mac;
    saveSrvCfg();
    g_forceRepoll = true;
    srvLog("[Settings] Player MAC: %s", mac.c_str());
}
void wifiSetSyncVolume(bool enabled) {
    g_srv.syncVolume = enabled;
    saveSrvCfg();
    srvLog("[Settings] Gruppenlautstärke: %s", enabled ? "an" : "aus");
}
String wifiGetPlaylistId()   { return g_playlistId; }
String wifiGetPlaylistName() { return g_playlistName; }
bool wifiSetPlaylist(const String& id, const String& name, bool loadNow) {
    g_playlistId   = id;
    g_playlistName = name;
    savePlaylistCfg();
    if (loadNow && !g_srv.playerMac.isEmpty())
        squeezeLoadPlaylist(g_srv.playerMac, id, name);
    srvLog("[Settings] Playlist: %s (%s)", name.c_str(), id.c_str());
    return true;
}

void wifiForceRepoll() {
    g_fastPollUntil = millis() + LMS_FAST_POLL_WINDOW_MS;
    g_forceRepoll = true;
}

void wifiBeginUserCommand() {
    g_userCommandActive = true;
    g_fastPollUntil = millis() + LMS_FAST_POLL_WINDOW_MS;
}

void wifiEndUserCommand(bool forceRepoll) {
    g_userCommandActive = false;
    if (forceRepoll) wifiForceRepoll();
}
