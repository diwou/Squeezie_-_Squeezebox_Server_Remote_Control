#include "squeezeapi.h"
#include "webserver.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <WiFi.h>

static String   g_host;
static uint16_t g_port = LYRION_DEFAULT_PORT;

int squeezeVolumeFromLms(int lmsVolume) {
    if (lmsVolume <= SQUEEZE_VOL_MIN) return 0;
    return constrain((int)lroundf((lmsVolume - SQUEEZE_VOL_MIN) * 100.0f
                                  / (100.0f - SQUEEZE_VOL_MIN)), 0, 100);
}

int squeezeVolumeToLms(int uiVolume) {
    int clamped = constrain(uiVolume, 0, 100);
    if (clamped <= 0) return SQUEEZE_VOL_MIN;
    return constrain((int)lroundf(SQUEEZE_VOL_MIN
                                  + clamped * (100.0f - SQUEEZE_VOL_MIN) / 100.0f),
                     SQUEEZE_VOL_MIN, 100);
}

void squeezeSetServer(const String& host, uint16_t port) {
    g_host = host;
    g_port = port;
}
String   squeezeGetHost() { return g_host; }
uint16_t squeezeGetPort() { return g_port; }

// ─── JSON-RPC ────────────────────────────────────────────────────────────────

// Sendet eine JSON-RPC-Anfrage an den Lyrion-Server.
// playerMac: "" für globale Kommandos
// cmdJson:   Array als JSON-String, z.B. ["players",0,100]
static bool jsonRpc(const String& playerMac, const String& cmdJson, JsonDocument& resp) {
    if (g_host.isEmpty()) return false;
    HTTPClient http;
    String url = "http://" + g_host + ":" + String(g_port) + "/jsonrpc.js";
    if (!http.begin(url)) return false;
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(2000);
    String body = "{\"id\":1,\"method\":\"slim.request\",\"params\":[\""
                  + playerMac + "\"," + cmdJson + "]}";
    int code = http.POST(body);
    if (code != 200) { http.end(); return false; }
    String payload = http.getString();
    http.end();
    return !deserializeJson(resp, payload);
}

static String makeTrackCoverKey(JsonObjectConst track) {
    String key;
    key.reserve(160);
    key += track["title"].as<String>();
    key += '\x1F';
    key += track["artist"].as<String>();
    key += '\x1F';
    key += track["album"].as<String>();
    key += '\x1F';
    key += String(track["tracknum"].as<int>());
    key += '\x1F';
    key += String((int)track["duration"].as<float>());
    return key;
}

// ─── Player-Liste ────────────────────────────────────────────────────────────

bool squeezeGetPlayers(std::vector<SqueeziePlayer>& out) {
    JsonDocument resp;
    if (!jsonRpc("", "[\"players\",0,100]", resp)) return false;
    JsonArray arr = resp["result"]["players_loop"].as<JsonArray>();
    if (arr.isNull()) return false;
    out.clear();
    for (JsonObject p : arr) {
        SqueeziePlayer pl;
        pl.mac       = p["playerid"].as<String>();
        pl.name      = p["name"].as<String>();
        pl.connected = p["connected"].as<int>() != 0;
        out.push_back(pl);
    }
    return true;
}

// ─── Status ──────────────────────────────────────────────────────────────────

bool squeezeGetStatus(const String& playerMac, SqueezieStatus& out) {
    JsonDocument resp;
    // Tags: a=artist, A=albumartist, l=album, K=artwork_url, c=coverid, d=duration, t=tracknum
    // SQUEEZE_PREFETCH+1 Einträge abrufen: [0]=aktuell, [1..N]=nächste für Prefetch
    if (!jsonRpc(playerMac, "[\"status\",\"-\"," + String(SQUEEZE_PREFETCH + 1) + ",\"tags:aAlKcdt\"]", resp)) return false;
    JsonObject r = resp["result"].as<JsonObject>();
    if (r.isNull()) return false;
    String mode     = r["mode"].as<String>();
    out.playing     = (mode == "play");
    out.volume      = constrain(r["mixer volume"].as<int>(), 0, 100);
    out.repeatMode  = r["playlist repeat"].as<int>();
    out.shuffleMode = r["playlist shuffle"].as<int>();
    out.position    = (int)r["time"].as<float>();
    out.playerName  = r["player_name"].as<String>();
    out.nextCount   = 0;
    JsonArray loop  = r["playlist_loop"].as<JsonArray>();
    if (!loop.isNull() && loop.size() > 0) {
        JsonObject t  = loop[0].as<JsonObject>();
        out.title     = t["title"].as<String>();
        out.artist    = t["artist"].as<String>();
        out.album     = t["album"].as<String>();
        out.trackNum  = t["tracknum"].as<int>();
        out.duration  = (int)t["duration"].as<float>();
        // Aktuelles Cover immer über den LMS-"current"-Endpoint holen.
        // So bleibt das Bild an den tatsächlich laufenden Track gebunden, auch wenn
        // playlist_loop/coverid beim Übergang kurz asynchron zu den Track-Metadaten ist.
        out.coverUrl = "http://" + g_host + ":" + String(g_port)
                       + "/music/current/cover_128x128_o.jpg?player=" + playerMac;
        out.coverKey = makeTrackCoverKey(t);
        // Cover-URLs der Folgetracks (coverid-basiert, da "current" nur den aktuellen liefert)
        for (size_t i = 1; i < loop.size() && out.nextCount < SQUEEZE_PREFETCH; i++) {
            JsonObject nextTrack = loop[i].as<JsonObject>();
            String cid = nextTrack["coverid"].as<String>();
            if (!cid.isEmpty() && cid != "0") {
                out.nextCoverUrls[out.nextCount] =
                    "http://" + g_host + ":" + String(g_port)
                    + "/music/" + cid + "/cover_128x128_f.jpg";
                out.nextCoverKeys[out.nextCount] = makeTrackCoverKey(nextTrack);
                out.nextCount++;
            }
        }
    }
    srvLog("[LMS] %s | vol=%d pos=%d pf=%d",
           out.title.isEmpty() ? "-" : out.title.c_str(),
           out.volume, out.position, out.nextCount);
    return true;
}

// ─── Steuerkommandos ─────────────────────────────────────────────────────────

bool squeezePlay(const String& mac)  { JsonDocument d; return jsonRpc(mac, "[\"play\"]", d); }
bool squeezePause(const String& mac) { JsonDocument d; return jsonRpc(mac, "[\"pause\",1]", d); }
bool squeezeNext(const String& mac)  { JsonDocument d; return jsonRpc(mac, "[\"playlist\",\"index\",\"+1\"]", d); }
bool squeezePrev(const String& mac)  { JsonDocument d; return jsonRpc(mac, "[\"playlist\",\"index\",\"-1\"]", d); }

bool squeezeSeek(const String& mac, int seconds) {
    JsonDocument d;
    return jsonRpc(mac, "[\"time\",\"" + String(seconds) + "\"]", d);
}

bool squeezeSetVolume(const String& mac, int vol) {
    JsonDocument d;
    return jsonRpc(mac, "[\"mixer\",\"volume\",\"" + String(squeezeVolumeToLms(vol)) + "\"]", d);
}

bool squeezeAdjustVolume(const String& mac, int delta) {
    JsonDocument d;
    String cmd = (delta >= 0) ? ("+" + String(delta)) : String(delta);
    return jsonRpc(mac, "[\"mixer\",\"volume\",\"" + cmd + "\"]", d);
}

bool squeezeSetRepeat(const String& mac, int mode) {
    JsonDocument d;
    return jsonRpc(mac, "[\"playlist\",\"repeat\",\"" + String(mode) + "\"]", d);
}

bool squeezeSetShuffle(const String& mac, int mode) {
    JsonDocument d;
    return jsonRpc(mac, "[\"playlist\",\"shuffle\",\"" + String(mode) + "\"]", d);
}

String squeezeCoverUrl(const String& coverId, int size) {
    if (g_host.isEmpty() || coverId.isEmpty()) return "";
    return "http://" + g_host + ":" + String(g_port)
           + "/music/" + coverId
           + "/cover_" + String(size) + "x" + String(size) + "_f.jpg";
}

// ─── Server-Discovery ────────────────────────────────────────────────────────
// 1. UDP-Broadcast an Port 3483 (Slim-Protokoll) — schnell wenn Server antwortet
// 2. TCP-Portscan der Prioritäts-IPs im lokalen Subnetz — zuverlässiger Fallback

bool squeezeDiscover(String& foundHost, uint16_t& foundPort, uint32_t timeoutMs) {
    uint32_t t0 = millis();

    // ── 1. UDP-Broadcast ─────────────────────────────────────────────────────
    {
        WiFiUDP udp;
        udp.begin(3484);
        const uint8_t pkt[] = {
            'e','I','P','A','D',0,'N','A','M','E',0,
            'S','q','u','e','e','z','i','e',0
        };
        udp.beginPacket(IPAddress(255, 255, 255, 255), 3483);
        udp.write(pkt, sizeof(pkt));
        udp.endPacket();
        while (millis() - t0 < 1500) {
            int len = udp.parsePacket();
            if (len > 0) {
                uint8_t buf[64];
                int n = udp.read(buf, sizeof(buf));
                if (n > 0 && buf[0] == 'E') {
                    foundHost = udp.remoteIP().toString();
                    foundPort = LYRION_DEFAULT_PORT;
                    udp.stop();
                    return true;
                }
            }
            delay(30);
        }
        udp.stop();
    }

    // ── 2. TCP-Portscan häufiger Server-IPs (kein vollständiger /24-Scan) ────
    IPAddress local = WiFi.localIP();
    if (!local || millis() - t0 >= timeoutMs) return false;

    // Typische Server-Adressen zuerst (.1, .2, .100-.110, .200-.205, .254)
    const int prio[] = {1, 2, 100, 101, 102, 103, 104, 105, 200, 201, 202, 254};
    WiFiClient c;
    for (int oct : prio) {
        if (millis() - t0 >= timeoutMs) break;
        if ((int)local[3] == oct) continue;
        IPAddress ip(local[0], local[1], local[2], oct);
        if (c.connect(ip, LYRION_DEFAULT_PORT, 400)) {
            c.stop();
            foundHost = ip.toString();
            foundPort = LYRION_DEFAULT_PORT;
            return true;
        }
        c.stop();
    }
    return false;
}
