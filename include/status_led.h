#pragma once
#include <Arduino.h>

// Onboard status LED — the human-facing health indicator called for in the
// To-Do (§4: booting / connecting-WiFi / connected-no-Pi / streaming /
// listen-only / error). Non-blocking: a low-priority task owns the blink timing
// and reads a shared state set by the network layer.
namespace led {

  enum State {
    BOOTING,      // firmware just came up
    WIFI,         // joining a network (station or fallback AP)
    NO_PI,        // WiFi up, but no active SLCAN stream to the Pi
    STREAMING,    // TCP connected, channel open, normal RX+TX
    LISTEN,       // channel open in listen-only mode (TX disabled)
  };

  void begin();              // configure the pin, start the blink task (BOOTING)
  void set(State s);         // set the normal operating state
  void fault(bool on);       // latch a hard fault (e.g. MCP2515 init failed);
                             // the ERROR pattern overrides `state` while set
}
