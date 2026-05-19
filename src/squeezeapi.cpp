#include "squeezeapi.h"
#include "webserver.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <WiFi.h>
#include <ESPmDNS.h>
// cspell:ignore slimproto EIPA

static String   g_host;
static uint16_t g_port = LYRION_DEFAULT_PORT;

int squeezeVolumeFromLms(int lmsVolume) {
    return constrain(abs(lmsVolume), 0, 100);
}

int squeezeVolumeToLms(int uiVolume) {
    return constrain(uiVolume, 0, 100);
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
static bool jsonRpc(const String& playerMac, const String& cmdJson, JsonDocument& resp,
                    uint32_t timeoutMs = 1200) {
    if (g_host.isEmpty()) return false;
    JsonDocument request;
    request["id"] = 1;
    request["method"] = "slim.request";
    JsonArray params = request["params"].to<JsonArray>();
    params.add(playerMac);
    JsonArray cmdArray = params.add<JsonArray>();
    JsonDocument cmdDoc;
    if (deserializeJson(cmdDoc, cmdJson)) return false;
    if (!cmdDoc.is<JsonArray>()) return false;
    for (JsonVariantConst item : cmdDoc.as<JsonArrayConst>()) cmdArray.add(item);

    HTTPClient http;
    String url;
    url.reserve(g_host.length() + cmdJson.length() + playerMac.length() + 32);
    url = "http://";
    url += g_host;
    url += ':';
    url += String(g_port);
    url += "/jsonrpc.js";
    if (!http.begin(url)) return false;
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(timeoutMs);
    String body;
    body.reserve(playerMac.length() + cmdJson.length() + 96);
    serializeJson(request, body);
    int code = http.POST(body);
    if (code != 200) { http.end(); return false; }
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        return false;
    }
    bool ok = !deserializeJson(resp, *stream);
    http.end();
    return ok;
}

static String coverUrlForId(const String& coverId, int size) {
    if (g_host.isEmpty() || coverId.isEmpty() || coverId == "0") return "";
    return "http://" + g_host + ":" + String(g_port)
           + "/music/" + coverId
           + "/cover_" + String(size) + "x" + String(size) + "_f.jpg";
}

static String currentCoverUrlForPlayer(const String& playerMac, int size) {
    if (g_host.isEmpty() || playerMac.isEmpty()) return "";
    return "http://" + g_host + ":" + String(g_port)
           + "/music/current/cover_" + String(size) + "x" + String(size)
           + "_f.jpg?player=" + playerMac;
}

static String trackKey(JsonObjectConst track) {
    String id = track["id"].as<String>();
    if (!id.isEmpty()) return id;
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

static void addUniqueString(std::vector<String>& out, const String& value) {
    if (value.isEmpty()) return;
    for (const String& existing : out) {
        if (existing == value) return;
    }
    out.push_back(value);
}

static bool isLikelyMacAddress(const String& value) {
    if (value.length() != 17) return false;
    for (int i = 0; i < 17; ++i) {
        char c = value[i];
        if ((i % 3) == 2) {
            if (c != ':') return false;
            continue;
        }
        bool hex = (c >= '0' && c <= '9')
                || (c >= 'a' && c <= 'f')
                || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

static void addUniqueMac(std::vector<String>& out, const String& value) {
    String mac = value;
    mac.trim();
    if (!isLikelyMacAddress(mac)) return;
    mac.toLowerCase();
    addUniqueString(out, mac);
}

static void appendCsvStrings(const String& csv, std::vector<String>& out) {
    int start = 0;
    while (start < csv.length()) {
        int comma = csv.indexOf(',', start);
        if (comma < 0) comma = csv.length();
        String item = csv.substring(start, comma);
        addUniqueMac(out, item);
        start = comma + 1;
    }
}

static String jsonStringField(JsonObjectConst obj, const char* key) {
    JsonVariantConst v = obj[key];
    if (v.isNull()) return "";
    return v.as<String>();
}

static String firstSyncString(JsonObjectConst obj) {
    const char* preferredKeys[] = {
        "sync_members",
        "_sync_members",
        "sync",
        "_sync",
        "syncgroups",
        "_syncgroups",
    };
    for (const char* key : preferredKeys) {
        String value = jsonStringField(obj, key);
        if (!value.isEmpty() && value != "-") return value;
    }
    for (JsonPairConst kv : obj) {
        String key = kv.key().c_str();
        if (key.indexOf("sync") < 0) continue;
        JsonVariantConst value = kv.value();
        if (value.isNull()) continue;
        String text = value.as<String>();
        if (!text.isEmpty() && text != "-") return text;
    }
    return "";
}

static void collectSyncMembersFromResponse(JsonObjectConst result, std::vector<String>& out) {
    String members = firstSyncString(result);
    if (members.isEmpty()) return;

    int marker = members.indexOf("sync_members:");
    if (marker >= 0) {
        int start = marker + 13;
        int end = members.indexOf(' ', start);
        if (end < 0) end = members.length();
        appendCsvStrings(members.substring(start, end), out);
        return;
    }

    appendCsvStrings(members, out);
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

bool squeezeGetSyncGroupMembers(const String& playerMac, std::vector<String>& out) {
    out.clear();
    addUniqueMac(out, playerMac);
    if (playerMac.isEmpty()) return false;

    JsonDocument resp;
    if (jsonRpc("", "[\"syncgroups\",\"?\"]", resp)) {
        JsonObject result = resp["result"].as<JsonObject>();
        if (!result.isNull()) collectSyncMembersFromResponse(result, out);
    }

    if (out.size() <= 1 && jsonRpc(playerMac, "[\"sync\",\"?\"]", resp)) {
        JsonObject result = resp["result"].as<JsonObject>();
        if (!result.isNull()) collectSyncMembersFromResponse(result, out);
    }

    return !out.empty();
}

// ─── Status ──────────────────────────────────────────────────────────────────

bool squeezeGetStatus(const String& playerMac, SqueezieStatus& out) {
    JsonDocument resp;
    // Tags: a=artist, A=albumartist, l=album, K=artwork_url, c=coverid, d=duration, t=tracknum
    if (!jsonRpc(playerMac, "[\"status\",\"-\",1,\"tags:aAlKcdt\"]",
                 resp, 800)) return false;
    JsonObject r = resp["result"].as<JsonObject>();
    if (r.isNull()) return false;
    String mode     = r["mode"].as<String>();
    int rawVolume   = r["mixer volume"].as<int>();
    out.playing     = (mode == "play");
    out.volume      = squeezeVolumeFromLms(rawVolume);
    out.muted       = rawVolume < 0;
    out.repeatMode  = r["playlist repeat"].as<int>();
    out.shuffleMode = r["playlist shuffle"].as<int>();
    out.position    = (int)r["time"].as<float>();
    out.playerName  = r["player_name"].as<String>();
    JsonArray loop  = r["playlist_loop"].as<JsonArray>();
    if (!loop.isNull() && loop.size() > 0) {
        JsonObject t  = loop[0].as<JsonObject>();
        out.title     = t["title"].as<String>();
        out.artist    = t["artist"].as<String>();
        out.album     = t["album"].as<String>();
        out.trackNum  = t["tracknum"].as<int>();
        out.duration  = (int)t["duration"].as<float>();
        String coverId = t["coverid"].as<String>();
        out.coverUrl = coverUrlForId(coverId, 128);
        out.coverKey = coverId.isEmpty() || coverId == "0" ? trackKey(t) : coverId;
        if (out.coverUrl.isEmpty()) out.coverUrl = currentCoverUrlForPlayer(playerMac, 128);
    }
    srvLog("[LMS] %s | vol=%d%s pos=%d",
           out.title.isEmpty() ? "-" : out.title.c_str(),
           out.volume, out.muted ? " muted" : "", out.position);
    return true;
}

// ─── Volume-Konvertierung ─────────────────────────────────────────────────────
// LMS liefert Volume als int (0–100), gelegentlich auch negative Werte bei
// JSON-Parsing-Fehlern. Der abs()-Aufruf unten fängt negative Werte ab, damit
// keine negativen Lautstärken im UI landen. Die Konvertierung ist ansonsten 1:1.

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
    return coverUrlForId(coverId, size);
}

// ─── Mute ────────────────────────────────────────────────────────────────────

bool squeezeToggleMute(const String& mac) {
    JsonDocument volumeResp;
    if (!jsonRpc(mac, "[\"mixer\",\"volume\",\"?\"]", volumeResp)) return false;
    int rawVolume = volumeResp["result"]["_volume"].as<int>();
    if (rawVolume == 0) return true;

    JsonDocument d;
    return jsonRpc(mac, "[\"mixer\",\"muting\",\"" + String(rawVolume < 0 ? "0" : "1") + "\"]", d);
}

bool squeezeSetMute(const String& mac, bool mute) {
    JsonDocument d;
    return jsonRpc(mac, "[\"mixer\",\"muting\",\"" + String(mute ? "1" : "0") + "\"]", d);
}

bool squeezeGetVolumeState(const String& mac, int& volume, bool& muted) {
    JsonDocument resp;
    if (!jsonRpc(mac, "[\"mixer\",\"volume\",\"?\"]", resp, 700)) return false;
    int rawVolume = resp["result"]["_volume"].as<int>();
    volume = squeezeVolumeFromLms(rawVolume);
    muted = rawVolume < 0;
    return true;
}

// ─── Playlists ───────────────────────────────────────────────────────────────

bool squeezeGetPlaylists(std::vector<SqueeziePlaylist>& out) {
    JsonDocument resp;
    if (!jsonRpc("", "[\"playlists\",0,200,\"tags:u\"]", resp)) return false;
    JsonArray arr = resp["result"]["playlists_loop"].as<JsonArray>();
    if (arr.isNull()) return false;
    out.clear();
    for (JsonObject p : arr) {
        SqueeziePlaylist pl;
        pl.id         = p["id"].as<String>();
        pl.name       = p["playlist"].as<String>();
        pl.trackCount = p["count"].as<int>();
        out.push_back(pl);
    }
    return true;
}

bool squeezeLoadPlaylist(const String& mac, const String& playlistId,
                         const String& playlistName) {
    (void)playlistName;
    JsonDocument d;
    return jsonRpc(mac, "[\"playlistcontrol\",\"cmd:load\",\"playlist_id:" + playlistId + "\"]", d);
}

// ─── Server-Discovery ────────────────────────────────────────────────────────
// 1. UDP-Broadcast an Port 3483 (Slim-Protokoll) — schnell wenn Server antwortet
// 2. TCP-Portscan der Prioritäts-IPs im lokalen Subnetz — zuverlässiger Fallback

bool squeezeDiscover(String& foundHost, uint16_t& foundPort, uint32_t timeoutMs) {
    auto parseJsonPort = [](const uint8_t* data, int len) -> uint16_t {
        if (!data || len <= 0) return LYRION_DEFAULT_PORT;
        for (int i = 0; i < len - 4; i++) {
            if (data[i] == 'J' && data[i + 1] == 'S' && data[i + 2] == 'O' && data[i + 3] == 'N') {
                int p = i + 4;
                while (p < len && (data[p] == 0 || data[p] == ':' || data[p] == '=' || data[p] == ' ')) p++;
                int port = 0;
                while (p < len && data[p] >= '0' && data[p] <= '9') {
                    port = (port * 10) + (data[p] - '0');
                    p++;
                    if (port > 65535) break;
                }
                if (port > 0 && port <= 65535) return (uint16_t)port;
            }
        }
        return LYRION_DEFAULT_PORT;
    };

    uint32_t t0 = millis();
    srvLog("[LMS] Discovery gestartet (Timeout %ums).", (unsigned)timeoutMs);

    // ── 1. UDP-Broadcast ─────────────────────────────────────────────────────
    {
        WiFiUDP udp;
        udp.begin(0);
        const uint8_t pktEIPA[] = {'e','I','P','A'};
        const uint8_t pktTlv[] = {
            'e','I','P','A','D',0,'N','A','M','E',0,
            'S','q','u','e','e','z','i','e',0,
            'J','S','O','N',0,'V','E','R','S',0,'U','U','I','D',0
        };
        const uint8_t pktResp[] = {'R','E','S','P'};
        const uint8_t pktLegacyD[18] = {'d',0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        IPAddress local = WiFi.localIP();
        IPAddress mask  = WiFi.subnetMask();
        IPAddress bcast(local[0] | (uint8_t)~mask[0],
                        local[1] | (uint8_t)~mask[1],
                        local[2] | (uint8_t)~mask[2],
                        local[3] | (uint8_t)~mask[3]);

        auto sendProbe = [&](const IPAddress& addr, const uint8_t* pkt, size_t len) {
            udp.beginPacket(addr, 3483);
            udp.write(pkt, len);
            udp.endPacket();
        };
        sendProbe(bcast, pktEIPA, sizeof(pktEIPA));
        sendProbe(bcast, pktTlv, sizeof(pktTlv));
        sendProbe(bcast, pktResp, sizeof(pktResp));
        sendProbe(bcast, pktLegacyD, sizeof(pktLegacyD));
        // Fallback für Netze, in denen nur 255.255.255.255 geroutet wird.
        IPAddress globalBcast(255, 255, 255, 255);
        sendProbe(globalBcast, pktEIPA, sizeof(pktEIPA));
        sendProbe(globalBcast, pktTlv, sizeof(pktTlv));
        sendProbe(globalBcast, pktResp, sizeof(pktResp));
        sendProbe(globalBcast, pktLegacyD, sizeof(pktLegacyD));

        uint32_t udpWaitMs = timeoutMs;
        if (udpWaitMs < 1500) udpWaitMs = 1500;
        if (udpWaitMs > 4000) udpWaitMs = 4000;
        while (millis() - t0 < udpWaitMs) {
            int len = udp.parsePacket();
            if (len > 0) {
                uint8_t buf[64];
                int n = udp.read(buf, sizeof(buf));
                if (n > 0) {
                    uint8_t b0 = buf[0];
                    if (!(b0 == 'E' || b0 == 'e' || b0 == 'D' || b0 == 'd' || b0 == 'R')) {
                        delay(15);
                        continue;
                    }
                    IPAddress candidate = udp.remoteIP();
                    uint16_t candidatePort = parseJsonPort(buf, n);
                    // Laut Slim-Discovery gilt die UDP-Quell-IP bereits als Server-Treffer.
                    // Kein zusätzlicher TCP-Probe hier, um Falsch-Negative zu vermeiden.
                    foundHost = candidate.toString();
                    foundPort = candidatePort;
                    srvLog("[LMS] Discovery via UDP: %s:%u", foundHost.c_str(), foundPort);
                    udp.stop();
                    return true;
                }
            }
            delay(30);
        }
        udp.stop();
    }

    // ── 2. mDNS-Fallback (_slimproto._tcp) ──────────────────────────────────
    if (millis() - t0 < timeoutMs) {
        static bool mdnsReady = false;
        if (!mdnsReady) mdnsReady = MDNS.begin("squeezie");
        int n = MDNS.queryService("slimproto", "tcp");
        for (int i = 0; i < n; i++) {
            IPAddress ip = MDNS.IP(i);
            if (!ip) continue;
            WiFiClient probe;
            if (probe.connect(ip, LYRION_DEFAULT_PORT, 220)) {
                probe.stop();
                foundHost = ip.toString();
                foundPort = LYRION_DEFAULT_PORT;
                srvLog("[LMS] Discovery via mDNS: %s:%u", foundHost.c_str(), foundPort);
                return true;
            }
        }
    }
    srvLog("[LMS] Discovery ohne Treffer beendet.");
    return false;
}
