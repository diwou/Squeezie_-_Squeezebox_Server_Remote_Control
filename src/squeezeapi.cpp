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
static bool jsonRpc(const String& playerMac, const String& cmdJson, JsonDocument& resp) {
    if (g_host.isEmpty()) return false;
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
    http.setTimeout(2000);
    String body;
    body.reserve(playerMac.length() + cmdJson.length() + 64);
    body = "{\"id\":1,\"method\":\"slim.request\",\"params\":[\"";
    body += playerMac;
    body += "\",";
    body += cmdJson;
    body += "]}";
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
           + "_o.jpg?player=" + playerMac;
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

static void appendCsvStrings(const String& csv, std::vector<String>& out) {
    int start = 0;
    while (start < csv.length()) {
        int comma = csv.indexOf(',', start);
        if (comma < 0) comma = csv.length();
        String item = csv.substring(start, comma);
        item.trim();
        addUniqueString(out, item);
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
    addUniqueString(out, playerMac);
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
    // SQUEEZE_PREFETCH+1 Einträge abrufen: [0]=aktuell, [1..N]=nächste für Prefetch
    if (!jsonRpc(playerMac, "[\"status\",\"-\"," + String(SQUEEZE_PREFETCH + 1) + ",\"tags:aAlKcdt\"]", resp)) return false;
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
    out.nextCount   = 0;
    JsonArray loop  = r["playlist_loop"].as<JsonArray>();
    if (!loop.isNull() && loop.size() > 0) {
        JsonObject t  = loop[0].as<JsonObject>();
        out.title     = t["title"].as<String>();
        out.artist    = t["artist"].as<String>();
        out.album     = t["album"].as<String>();
        out.trackNum  = t["tracknum"].as<int>();
        out.duration  = (int)t["duration"].as<float>();
        out.coverUrl = currentCoverUrlForPlayer(playerMac, 128);
        out.coverKey = trackKey(t);
        // Cover-URLs der Folgetracks
        for (size_t i = 1; i < loop.size() && out.nextCount < SQUEEZE_PREFETCH; i++) {
            JsonObject nextTrack = loop[i].as<JsonObject>();
            String nextCid = nextTrack["coverid"].as<String>();
            String nextUrl = coverUrlForId(nextCid, 128);
            if (!nextUrl.isEmpty()) {
                out.nextCoverUrls[out.nextCount] = nextUrl;
                out.nextCoverKeys[out.nextCount] = nextCid;
                out.nextCount++;
            }
        }
    }
    srvLog("[LMS] %s | vol=%d%s pos=%d pf=%d",
           out.title.isEmpty() ? "-" : out.title.c_str(),
           out.volume, out.muted ? " muted" : "", out.position, out.nextCount);
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
