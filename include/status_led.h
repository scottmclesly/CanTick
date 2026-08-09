#pragma once
#include <stdint.h>
// This header stays free of Arduino types. The panel maps a State to a color,
// and the host test build compiles that map.

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

  // An output backend writes one blink phase. `on` is the phase that the
  // pattern for `s` gives now.
  //
  // The state machine stays in this layer. A backend adds an output only. Do
  // not write a second state machine (CLAUDE.md "The matrix is the status LED
  // on C6").
  //
  // The default backend drives the onboard LED, which the S3 variant has. The
  // C6 has no usable onboard LED, thus that variant installs a matrix backend.
  using Output = void (*)(State s, bool fault, bool on);
  void setOutput(Output out);
}
