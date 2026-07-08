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
  uint32_t    g_rx = 0, g_tx = 0, g_drop = 0;

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

  // The drain: whole-core polling loop. On the S3 this is pinned to core 1 so it
  // never contends with WiFi/TCP (core 0). Fast enough for a 250 k N2K bus.
  void rxTask(void *) {
    CanFrame f;
    for (;;) {
      if (g_open && mcp.checkReceive() == CAN_MSGAVAIL) {
        unsigned long id = 0; byte ext = 0, len = 0;
        if (mcp.readMsgBuf(&id, &ext, &len, f.data) == CAN_OK) {
          f.id  = (uint32_t)id;
          f.ext = ext ? 1 : 0;
          f.rtr = mcp.isRemoteRequest() ? 1 : 0;
          f.dlc = len > 8 ? 8 : len;
          taskENTER_CRITICAL(&g_mux); g_rx++; taskEXIT_CRITICAL(&g_mux);
          if (g_sink) g_sink(f);
        }
        // tight loop while frames are pending
      } else {
        vTaskDelay(1);   // ~1 ms yield when the bus is idle
      }
      // TODO(hw): read MCP2515 EFLG (RX0OVR/RX1OVR) and fold into g_drop.
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
  taskENTER_CRITICAL(&g_mux); g_drop++; taskEXIT_CRITICAL(&g_mux);
  return false;
}

uint32_t rxCount()   { return g_rx; }
uint32_t txCount()   { return g_tx; }
uint32_t dropCount() { return g_drop; }

}  // namespace canlink
