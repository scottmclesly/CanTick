// CanTick — remote CAN node firmware entry point.
// XIAO ESP32-S3 + Seeed CAN Breakout (MCP2515, 16 MHz, CS=D7).
//
// Layering (all against the contract in PROTOCOL.md):
//   can_link      MCP2515 driver, dedicated-core polling RX, bounded single TX
//   slcan         LAWICEL ASCII codec (the subset slcand drives)
//   net_transport WiFi station/fallback, SLCAN-over-TCP client, UDP heartbeat
//   provisioning  USB-CDC CTK1 framed protocol -> NVS
//   nvs_store     persisted config

#include <Arduino.h>
#include "config.h"
#include "nvs_store.h"
#include "can_link.h"
#include "net_transport.h"
#include "provisioning.h"

// RX sink: bus frame -> TCP send queue. Runs in the CAN core-1 task context.
static void onBusFrame(const CanFrame &f) { net::enqueueRx(f); }

void setup() {
  Serial.begin(115200);          // native USB CDC — the provisioning channel
  delay(200);

  nvs::begin();
  CtConfig c = nvs::load();

  // CAN up at the stored bitrate/mode. If this fails, check wiring + that the
  // crystal really is 16 MHz (CANTICK_MCP_CLOCK).
  if (!canlink::begin(c.bitrate, c.listen)) {
    Serial.println("[cantick] MCP2515 init failed");   // plain log; Pi ignores non-CTK1 lines
  }
  canlink::onFrame(onBusFrame);
  canlink::startRxTask();        // pinned to core 1

  net::begin();                  // WiFi + TCP + heartbeat tasks on core 0
  prov::begin();
}

void loop() {
  prov::poll();                  // service USB-CDC provisioning frames
  delay(5);
}
