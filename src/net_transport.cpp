#include "net_transport.h"
#include "config.h"
#include "slcan.h"
#include "nvs_store.h"
#include "status_led.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
  WiFiClient   g_tcp;
  WiFiUDP      g_udp;
  QueueHandle_t g_rxq = nullptr;    // CanFrame -> TCP stream (bounded ring buffer)
  bool         g_chanOpen = false;  // SLCAN channel opened by slcand
  bool         g_mdnsUp   = false;  // MDNS.begin() succeeded (init once per link)
  IPAddress    g_piIp;              // Pi address resolved by netTask, reused by heartbeat
  volatile uint32_t g_netDrop = 0;  // outbound-queue-full drops (single writer: RX task)

  const IPAddress ZERO_IP((uint32_t)0);

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

  // Bring mDNS up exactly once while WiFi is connected (was previously re-begun
  // on every resolve, from two tasks — a needless race).
  void ensureMdns() {
    if (g_mdnsUp || WiFi.status() != WL_CONNECTED) return;
    if (MDNS.begin(nvs::deviceName().c_str())) g_mdnsUp = true;
  }
  void dropMdns() {
    if (g_mdnsUp) { MDNS.end(); g_mdnsUp = false; }
  }

  // Resolve the Pi: mDNS `scottina` on the .local domain, else the fixed AP
  // gateway. Only netTask calls this; the heartbeat reuses the cached result.
  IPAddress resolvePi() {
    IPAddress ip;
    ensureMdns();
    if (g_mdnsUp) {
      String host = CANTICK_PI_HOST;              // "scottina.local"
      int dot = host.indexOf(".local");
      if (dot > 0) host = host.substring(0, dot); // ESPmDNS wants the bare label
      ip = MDNS.queryHost(host.c_str());
      if (ip != ZERO_IP) return ip;
    }
    ip.fromString(CANTICK_PI_FALLBACK_IP);
    return ip;
  }

  // ── SLCAN-over-TCP client: connect, pump slcand <-> CAN ─────────────────
  void handleLine(const String &line) {
    slcan::ParseResult pr = slcan::parseLine(line);
    switch (pr.action) {
      case slcan::SET_BITRATE:
        led::fault(!canlink::begin(pr.bitrate, canlink::isListenOnly()));  // clear fault on good re-init
        break;
      case slcan::OPEN:        canlink::setListenOnly(false); g_chanOpen = true;    break;
      case slcan::OPEN_LISTEN: canlink::setListenOnly(true);  g_chanOpen = true;    break;
      case slcan::CLOSE:       g_chanOpen = false;                                  break;
      case slcan::TX_FRAME:
        // SAFETY INVARIANT (PROTOCOL §1/§6): listen-only transmits NOTHING and
        // NAKs the frame (BELL). Otherwise hand it to the single TX primitive.
        if (canlink::isListenOnly()) pr.reply = String('\a');
        else                         canlink::send(pr.frame);
        break;
      default: break;
    }
    if (pr.reply.length()) g_tcp.print(pr.reply);
  }

  void netTask(void *) {
    String   rxLine;
    uint32_t backoff = CANTICK_TCP_RETRY_MS;
    for (;;) {
      if (WiFi.status() != WL_CONNECTED) {
        led::set(led::WIFI);
        dropMdns();                       // stale after a disconnect; re-begin on reconnect
        connectWiFi();
        delay(CANTICK_WIFI_RETRY_MS);
        continue;
      }

      if (!g_tcp.connected()) {
        led::set(led::NO_PI);
        g_piIp = resolvePi();
        if (!g_tcp.connect(g_piIp, CANTICK_SLCAN_TCP_PORT)) {
          delay(backoff);
          uint32_t next = backoff * 2;    // exponential backoff, capped
          backoff = next > CANTICK_TCP_BACKOFF_MAX_MS ? CANTICK_TCP_BACKOFF_MAX_MS : next;
          continue;
        }
        backoff = CANTICK_TCP_RETRY_MS;   // reset on a good connect
        rxLine  = "";
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

      // reflect the live state on the status LED
      led::set(g_chanOpen ? (canlink::isListenOnly() ? led::LISTEN : led::STREAMING)
                          : led::NO_PI);
      delay(1);
    }
  }

  // ── UDP heartbeat (send-only) ───────────────────────────────────────────
  void heartbeatTask(void *) {
    for (;;) {
      if (WiFi.status() == WL_CONNECTED) {
        char buf[220];
        const char *mode = !g_chanOpen ? "closed" : (canlink::isListenOnly() ? "listen" : "normal");
        uint32_t drop = canlink::dropCount() + g_netDrop;   // MCP overflow + queue-full
        snprintf(buf, sizeof(buf),
          "{\"v\":%d,\"name\":\"%s\",\"fw\":\"%s\",\"up\":%lu,\"bitrate\":%lu,"
          "\"mode\":\"%s\",\"rx\":%lu,\"tx\":%lu,\"drop\":%lu,\"rssi\":%d}",
          CANTICK_CONTRACT_VERSION, nvs::deviceName().c_str(), CANTICK_FW_VERSION,
          (unsigned long)(millis() / 1000), (unsigned long)nvs::load().bitrate,
          mode, (unsigned long)canlink::rxCount(), (unsigned long)canlink::txCount(),
          (unsigned long)drop, WiFi.RSSI());
        IPAddress dst = g_piIp;
        if (dst == ZERO_IP) dst.fromString(CANTICK_PI_FALLBACK_IP);
        g_udp.beginPacket(dst, CANTICK_HEARTBEAT_UDP_PORT);
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
  if (xQueueSend(g_rxq, &f, 0) == pdTRUE) return;
  // Queue full → drop-oldest: discard the stalest frame to make room, count it,
  // then enqueue the fresh one. Keeps latency bounded under a network stall.
  CanFrame stale;
  if (xQueueReceive(g_rxq, &stale, 0) == pdTRUE) {
    g_netDrop = g_netDrop + 1;         // single writer (the CAN RX task)
    xQueueSend(g_rxq, &f, 0);
  }
}

uint32_t dropCount() { return g_netDrop; }

void applyCredentials() { dropMdns(); WiFi.disconnect(); connectWiFi(); }

}  // namespace net
