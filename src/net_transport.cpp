#include "net_transport.h"
#include "config.h"
#include "slcan.h"
#include "nvs_store.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
  WiFiClient   g_tcp;
  WiFiUDP      g_udp;
  QueueHandle_t g_rxq = nullptr;    // CanFrame -> TCP stream
  bool         g_chanOpen = false;  // SLCAN channel opened by slcand

  // ── WiFi: try primary station, then fall back to the Pi AP ──────────────
  bool connectWiFi() {
    CtConfig c = nvs::load();
    WiFi.mode(WIFI_STA);

    if (c.priSsid.length()) {
      WiFi.begin(c.priSsid.c_str(), c.priPsk.c_str());
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);
      if (WiFi.status() == WL_CONNECTED) return true;
    }
    if (c.fbSsid.length()) {
      WiFi.begin(c.fbSsid.c_str(), c.fbPsk.c_str());
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
  }

  IPAddress resolvePi() {
    IPAddress ip;
    // 1) mDNS scottina.local
    if (MDNS.begin(nvs::deviceName().c_str())) {
      ip = MDNS.queryHost(String(CANTICK_PI_HOST).c_str());
      if (ip) return ip;
    }
    // 2) fixed AP gateway fallback
    ip.fromString(CANTICK_PI_FALLBACK_IP);
    return ip;
  }

  // ── SLCAN-over-TCP client: connect, pump slcand <-> CAN ─────────────────
  void handleLine(const String &line) {
    slcan::ParseResult pr = slcan::parseLine(line);
    switch (pr.action) {
      case slcan::SET_BITRATE: canlink::begin(pr.bitrate, canlink::isListenOnly()); break;
      case slcan::OPEN:        canlink::setListenOnly(false); g_chanOpen = true;    break;
      case slcan::OPEN_LISTEN: canlink::setListenOnly(true);  g_chanOpen = true;    break;
      case slcan::CLOSE:       g_chanOpen = false;                                  break;
      case slcan::TX_FRAME:
        // SAFETY: canlink::send() no-ops in listen-only mode.
        canlink::send(pr.frame);
        break;
      default: break;
    }
    if (pr.reply.length()) g_tcp.print(pr.reply);
  }

  void netTask(void *) {
    String rxLine;
    for (;;) {
      if (WiFi.status() != WL_CONNECTED) { connectWiFi(); delay(CANTICK_WIFI_RETRY_MS); continue; }

      if (!g_tcp.connected()) {
        IPAddress pi = resolvePi();
        if (!g_tcp.connect(pi, CANTICK_SLCAN_TCP_PORT)) { delay(CANTICK_TCP_RETRY_MS); continue; }
        rxLine = "";
      }

      // slcand -> us: read available bytes, split on '\r'
      while (g_tcp.available()) {
        char ch = (char)g_tcp.read();
        if (ch == '\r') { handleLine(rxLine); rxLine = ""; }
        else if (rxLine.length() < 64) rxLine += ch;
      }

      // bus -> slcand: drain the RX queue to the TCP stream
      CanFrame f;
      while (g_chanOpen && g_rxq && xQueueReceive(g_rxq, &f, 0) == pdTRUE) {
        g_tcp.print(slcan::encodeFrame(f));
      }
      delay(1);
    }
  }

  // ── UDP heartbeat (send-only) ───────────────────────────────────────────
  void heartbeatTask(void *) {
    for (;;) {
      if (WiFi.status() == WL_CONNECTED) {
        char buf[220];
        const char *mode = !g_chanOpen ? "closed" : (canlink::isListenOnly() ? "listen" : "normal");
        snprintf(buf, sizeof(buf),
          "{\"v\":%d,\"name\":\"%s\",\"fw\":\"%s\",\"up\":%lu,\"bitrate\":%lu,"
          "\"mode\":\"%s\",\"rx\":%lu,\"tx\":%lu,\"drop\":%lu,\"rssi\":%d}",
          CANTICK_CONTRACT_VERSION, nvs::deviceName().c_str(), CANTICK_FW_VERSION,
          (unsigned long)(millis() / 1000), (unsigned long)nvs::load().bitrate,
          mode, (unsigned long)canlink::rxCount(), (unsigned long)canlink::txCount(),
          (unsigned long)canlink::dropCount(), WiFi.RSSI());
        IPAddress pi = resolvePi();
        g_udp.beginPacket(pi, CANTICK_HEARTBEAT_UDP_PORT);
        g_udp.write((const uint8_t *)buf, strlen(buf));
        g_udp.endPacket();
      }
      delay(CANTICK_HEARTBEAT_MS);
    }
  }
}

namespace net {

void begin() {
  g_rxq = xQueueCreate(CANTICK_RX_QUEUE_LEN, sizeof(CanFrame));
  connectWiFi();
  xTaskCreatePinnedToCore(netTask,       "net",  8192, nullptr, 8, nullptr, 0);
  xTaskCreatePinnedToCore(heartbeatTask, "hb",   4096, nullptr, 3, nullptr, 0);
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
int  rssi()          { return WiFi.RSSI(); }

void enqueueRx(const CanFrame &f) {
  if (!g_rxq) return;
  if (xQueueSend(g_rxq, &f, 0) != pdTRUE) {
    // queue full: frame dropped. Counted via canlink drop path in a later rev.
  }
}

void applyCredentials() { WiFi.disconnect(); connectWiFi(); }

}  // namespace net
