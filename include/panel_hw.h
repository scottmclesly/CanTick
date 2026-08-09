#pragma once
#include <stdint.h>

// ── Panel hardware ───────────────────────────────────────────────────────────
// The only file that touches the panel hardware. It owns the Adafruit NeoPixel
// object, it installs itself behind the panel::Driver seam, and it owns the
// matrix task.
//
// Every rendering decision lives in panel, strips, cards and matrix. Those units
// are pure and the host tests cover them. This file adds the hardware and the
// clock, and nothing else.
namespace panelhw {

  // Start the strip, install the driver, and take the counter marks. Call this
  // after canlink::begin(), thus the first delta reads a live counter.
  void begin(uint32_t bitrate);

  // Raise the two splash cards and run them to the end, here on the caller's
  // task. DISPLAY.md §7: the splash finishes before the WiFi start, thus no
  // animation runs through a connect attempt.
  void runSplash();

  // Raise the card for the configured bus speed (DISPLAY.md §5).
  void raiseBusSpeedCard();

  // Start the 20 Hz matrix task. A WS2812B write is timing-critical, thus the
  // task runs on core 0 and never against the CAN drain on core 1.
  void startTask();

}  // namespace panelhw
