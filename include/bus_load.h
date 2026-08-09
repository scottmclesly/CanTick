#pragma once
// ── Bus load estimate ────────────────────────────────────────────────────────
// DISPLAY.md §3 rule 12 is the authority:
//
//   load = frames_per_second x 111 / bitrate
//
// 111 is the bit count of an 8-byte frame with stuffing. The number is an
// estimate, not a measurement.
//
// This unit is pure. It gives a number, not a picture. It reads no clock, thus
// the caller passes the time in and the host test drives it.

#include <stdint.h>
#include "config.h"

namespace busload {

// The load bands that DISPLAY.md §3 and §4 act on. The strip renderer reads the
// band. It does not read the raw percent for a threshold decision.
// The BAND_ prefix is not decoration. Arduino defines LOW and HIGH as macros,
// and a macro ignores a namespace, thus a bare LOW here breaks every firmware
// file that includes Arduino.h.
enum Band : uint8_t {
  BAND_LOW   = 0,  // below 50 %: slow strip step
  BAND_MID   = 1,  // 50 % and above: fast strip step, strip is full
  BAND_HIGH  = 2,  // 90 % to 95 %: darker shade pair
  BAND_PULSE = 3,  // 95 % and above: solid line, and it pulses
};

// The load as a whole percent, from DISPLAY.md §3 rule 12. A bitrate of 0 gives
// 0, because an unconfigured bus has no load to report. The value is not capped
// at 100: a number above 100 means the configured bitrate is wrong.
uint32_t percent(uint32_t framesPerSecond, uint32_t bitrate);

// Put the state machine back in LOW with no pending change.
void reset();

// Feed one sample. `nowMs` is a monotonic millisecond count.
//
// A threshold has a 5 % band on each side. A load inside that band changes
// nothing. A load outside it starts a pending change, and the change lands only
// after the load stays outside for the full 500 ms dwell. A load that comes
// back inside the band cancels the pending change.
Band update(uint32_t loadPercent, uint32_t nowMs);

// The band in force now.
Band band();

}  // namespace busload
