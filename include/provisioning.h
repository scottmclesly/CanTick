#pragma once
#include <Arduino.h>

// USB-CDC provisioning: parses CRC-checked `CTK1|...` lines from the Pi, stages
// settings, and commits to NVS. Ignores all non-CTK1 / bad-CRC input.
// See PROTOCOL.md §4.
namespace prov {
  void begin();
  void poll();     // call from loop(); reads Serial, handles one line at a time
}
