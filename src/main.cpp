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
#include "status_led.h"
#include "panel.h"
#include "panel_hw.h"

// RX sink: bus frame -> TCP send queue. Runs in the CAN core-1 task context.
static void onBusFrame(const CanFrame &f) { net::enqueueRx(f); }

void setup() {
  Serial.begin(115200);          // native USB CDC — the provisioning channel
  delay(200);

  led::begin();                  // status LED task (starts in BOOTING)

  nvs::begin();
  CtConfig c = nvs::load();

  // Plain log line; the Pi ignores every line that does not start with CTK1|.
  // STATUS carries no bitrate, thus this is the only readback of the configured
  // speed without WiFi. The bus-speed card color follows this number.
  Serial.printf("[cantick] bitrate=%lu listen=%u\n",
                (unsigned long)c.bitrate, (unsigned)c.listen);

  // CAN up at the stored bitrate/mode. If this fails, check wiring + that the
  // crystal really is 16 MHz (CANTICK_MCP_CLOCK).
  if (!canlink::begin(c.bitrate, c.listen)) {
    Serial.println("[cantick] MCP2515 init failed");   // plain log; Pi ignores non-CTK1 lines
    led::fault(true);            // hard fault: fast blink until a good CAN init
  }
  canlink::onFrame(onBusFrame);
  canlink::startRxTask();        // pinned to core 1

  panelhw::begin(c.bitrate);     // NeoPixel up, driver installed behind panel

#if CANTICK_STATUS_ON_MATRIX
  // The C6 has no usable onboard LED, thus the matrix carries the status pixel
  // (DISPLAY.md §10). The state machine stays in status_led.
  led::setOutput(panel::statusPixelBackend);
#endif

  panelhw::runSplash();          // DISPLAY.md §7: it finishes before WiFi starts
  panelhw::raiseBusSpeedCard();
  panelhw::startTask();          // core 0, priority 4, 20 Hz

#if CANTICK_PANEL_DEMO
  // The demo is a panel bench, not a device. There is no Pi and no bus, thus a
  // WiFi retry flood would only drown the log.
  Serial.println("[cantick] panel demo build: WiFi is off");
#else
  net::begin();                  // WiFi + TCP + heartbeat tasks on core 0
#endif
  prov::begin();
}

void loop() {
  prov::poll();                  // service USB-CDC provisioning frames
  delay(5);
}
