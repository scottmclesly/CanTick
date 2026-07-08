#include "can_link.h"
#include "config.h"
#include <SPI.h>
#include <mcp_can.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
  MCP_CAN     mcp(CANTICK_CAN_CS_PIN);
  CanRxSink   g_sink        = nullptr;
  volatile bool g_listen    = false;
  volatile bool g_open      = false;
  portMUX_TYPE g_mux        = portMUX_INITIALIZER_UNLOCKED;
  uint32_t    g_rx = 0, g_tx = 0, g_overflow = 0;
  bool        g_prevOvr     = false;   // edge state for MCP RX-overflow detection

  // coryjfowler speed code from a numeric bitrate. Extend as needed.
  bool speedCode(uint32_t bitrate, uint8_t &code) {
    switch (bitrate) {
      case 10000:   code = CAN_10KBPS;  return true;
      case 20000:   code = CAN_20KBPS;  return true;
      case 50000:   code = CAN_50KBPS;  return true;
      case 100000:  code = CAN_100KBPS; return true;
      case 125000:  code = CAN_125KBPS; return true;
      case 250000:  code = CAN_250KBPS; return true;
      case 500000:  code = CAN_500KBPS; return true;
      case 1000000: code = CAN_1000KBPS;return true;
      default: return false;
    }
  }

  // Sample the MCP2515 EFLG register for an RX FIFO overflow (RX0OVR/RX1OVR).
  // The coryjfowler driver exposes getError() (reads EFLG) but no public flag
  // clear, so we count on the rising edge: one increment per overflow *event*
  // (a lower bound on frames lost inside the controller). Under a dedicated-core
  // drain at 250 k this should stay 0; the dominant drop point is the outbound
  // network queue, counted separately in net_transport.
  void pollOverflow() {
    bool ovr = (mcp.getError() & (MCP_EFLG_RX0OVR | MCP_EFLG_RX1OVR)) != 0;
    if (ovr && !g_prevOvr) {
      taskENTER_CRITICAL(&g_mux); g_overflow++; taskEXIT_CRITICAL(&g_mux);
    }
    g_prevOvr = ovr;
  }

  // The drain: whole-core polling loop. On the S3 this is pinned to core 1 so it
  // never contends with WiFi/TCP (core 0). Fast enough for a 250 k N2K bus.
  void rxTask(void *) {
    CanFrame f;
    uint32_t lastEflg = 0;
    for (;;) {
      if (g_open && mcp.checkReceive() == CAN_MSGAVAIL) {
        unsigned long id = 0; byte ext = 0, len = 0;
        if (mcp.readMsgBuf(&id, &ext, &len, f.data) == CAN_OK) {
          f.id  = (uint32_t)id;
          f.ext = ext ? 1 : 0;
          f.rtr = 0;
          f.dlc = len > 8 ? 8 : len;
          taskENTER_CRITICAL(&g_mux); g_rx++; taskEXIT_CRITICAL(&g_mux);
          if (g_sink) g_sink(f);
        }
        // tight loop while frames are pending
      } else {
        vTaskDelay(1);   // ~1 ms yield when the bus is idle
      }
      // Sample the overflow flag on a light cadence (SPI read, off the hot path).
      uint32_t now = millis();
      if (g_open && now - lastEflg >= CANTICK_EFLG_POLL_MS) { pollOverflow(); lastEflg = now; }
    }
  }
}

namespace canlink {

bool begin(uint32_t bitrate, bool listenOnly) {
  uint8_t code;
  if (!speedCode(bitrate, code)) code = CAN_250KBPS;   // safe default
  if (mcp.begin(MCP_ANY, code, CANTICK_MCP_CLOCK) != CAN_OK) return false;
  g_listen = listenOnly;
  mcp.setMode(listenOnly ? MCP_LISTENONLY : MCP_NORMAL);
  g_open = true;
  return true;
}

void setListenOnly(bool on) {
  g_listen = on;
  mcp.setMode(on ? MCP_LISTENONLY : MCP_NORMAL);
}
bool isListenOnly() { return g_listen; }

void onFrame(CanRxSink sink) { g_sink = sink; }

void startRxTask() {
  // pin to core 1; core 0 runs the Arduino/WiFi stack
  xTaskCreatePinnedToCore(rxTask, "can_rx", 4096, nullptr, 10, nullptr, 1);
}

bool send(const CanFrame &f) {
  if (g_listen || !g_open) return false;            // SAFETY: no TX when listening
  byte r = mcp.sendMsgBuf(f.id, f.ext ? 1 : 0, f.dlc, (byte *)f.data);
  if (r == CAN_OK) { taskENTER_CRITICAL(&g_mux); g_tx++; taskEXIT_CRITICAL(&g_mux); return true; }
  // A failed bus TX is not a "dropped RX frame": the contract's `drop` field
  // counts frames lost on the way to the Pi (overflow / outbound queue full),
  // so a TX error is reported only via the send() return, not folded into drop.
  return false;
}

uint32_t rxCount()   { return g_rx; }
uint32_t txCount()   { return g_tx; }
uint32_t dropCount() { return g_overflow; }   // MCP2515 RX FIFO overflow events

}  // namespace canlink
