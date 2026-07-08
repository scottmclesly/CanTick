#include "config.h"
#include "provisioning.h"
#include "nvs_store.h"
#include "net_transport.h"
#include <WiFi.h>

namespace {
  String   g_line;
  CtConfig g_stage;      // staged edits, applied on COMMIT
  bool     g_haveStage = false;

  // CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) — matches PROTOCOL.md appendix.
  uint16_t crc16(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
      crc ^= (uint16_t)d[i] << 8;
      for (int b = 0; b < 8; b++)
        crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
  }

  int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  }
  String b64decode(const String &in) {
    String out; int val = 0, bits = -8;
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      if (c == '=') break;
      int v = b64val(c); if (v < 0) continue;
      val = (val << 6) | v; bits += 6;
      if (bits >= 0) { out += (char)((val >> bits) & 0xFF); bits -= 8; }
    }
    return out;
  }

  // pull the value for key=... out of the '|'-delimited field list
  String field(const String &line, const String &key) {
    int k = line.indexOf("|" + key + "=");
    if (k < 0) return "";
    int s = k + 1 + key.length() + 1;
    int e = line.indexOf('|', s);
    return line.substring(s, e < 0 ? line.length() : e);
  }

  void reply(const String &s) { Serial.print("CTK1|"); Serial.print(s); Serial.print('\n'); }
  void ack(const String &cmd)             { reply("ACK|cmd=" + cmd); }
  void nak(const String &cmd, const char *e) { reply(String("NAK|cmd=") + cmd + "|err=" + e); }

  void sendStatus() {
    String s = "STATUS|name=" + nvs::deviceName() + "|fw=" + CANTICK_FW_VERSION +
               "|wifi=" + (net::wifiConnected() ? "connected" : "down") +
               "|ip=" + (net::wifiConnected() ? WiFi.localIP().toString() : String("0.0.0.0")) +
               "|prov=" + (nvs::load().priSsid.length() ? "1" : "0");
    reply(s);   // NB: never includes any PSK
  }

  void handle(const String &line) {
    // must start with CTK1| and carry a trailing |CRC=xxxx
    if (!line.startsWith("CTK1|")) return;                 // ignore noise
    int crcPos = line.lastIndexOf("|CRC=");
    if (crcPos < 0) return;
    String body = line.substring(0, crcPos);
    uint16_t want = (uint16_t)strtol(line.substring(crcPos + 5).c_str(), nullptr, 16);
    if (crc16((const uint8_t *)body.c_str(), body.length()) != want) { nak("?", "crc"); return; }

    // command is the 2nd field: CTK1|<CMD>|...
    int c1 = line.indexOf('|'); int c2 = line.indexOf('|', c1 + 1);
    String cmd = line.substring(c1 + 1, c2 < 0 ? crcPos : c2);

    if (cmd == "GET_STATUS") { sendStatus(); return; }

    if (cmd == "SET_CREDS") {
      String slot = field(line, "slot");
      String ssid = b64decode(field(line, "ssid"));
      String psk  = b64decode(field(line, "psk"));
      if (!g_haveStage) { g_stage = nvs::load(); g_haveStage = true; }
      if (slot == "primary")      { g_stage.priSsid = ssid; g_stage.priPsk = psk; }
      else if (slot == "fallback"){ g_stage.fbSsid  = ssid; g_stage.fbPsk  = psk; }
      else { nak(cmd, "badfield"); return; }
      ack(cmd); return;
    }

    if (cmd == "SET_NET") {
      if (!g_haveStage) { g_stage = nvs::load(); g_haveStage = true; }
      String br = field(line, "bitrate"); if (br.length()) g_stage.bitrate = br.toInt();
      String lo = field(line, "listen_only"); if (lo.length()) g_stage.listen = (lo == "1");
      ack(cmd); return;
    }

    if (cmd == "COMMIT") {
      if (g_haveStage) { nvs::save(g_stage); g_haveStage = false; }
      ack(cmd);
      net::applyCredentials();
      return;
    }

    nak(cmd, "badfield");
  }
}

namespace prov {
  void begin() { /* Serial already started in main setup() */ }

  void poll() {
    while (Serial.available()) {
      char ch = (char)Serial.read();
      if (ch == '\n' || ch == '\r') {
        if (g_line.length()) handle(g_line);
        g_line = "";
      } else if (g_line.length() < 400) {
        g_line += ch;
      }
    }
  }
}
