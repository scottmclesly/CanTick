#include "bus_load.h"

namespace {

// Threshold edges, from DISPLAY.md §3 rule 15. A threshold carries a 5 % band on
// each side, thus the dead band around 50 % runs from 45 % to 55 %.
constexpr uint32_t RISE_TO_MID   = CANTICK_LOAD_BAND_MID_PCT   + CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t FALL_TO_LOW   = CANTICK_LOAD_BAND_MID_PCT   - CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t RISE_TO_HIGH  = CANTICK_LOAD_BAND_HIGH_PCT  + CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t FALL_TO_MID   = CANTICK_LOAD_BAND_HIGH_PCT  - CANTICK_LOAD_HYSTERESIS_PCT;
// DISPLAY.md §3 rule 15: the 5 % band applies to every threshold except this
// one. A band here is wider than the gap to the 90 % threshold, which puts the
// pulse ramp out of reach on a rising load. The 500 ms dwell alone holds this
// edge steady.
constexpr uint32_t PULSE_EDGE    = CANTICK_LOAD_BAND_PULSE_PCT;

busload::Band g_band = busload::BAND_LOW;
busload::Band g_pending = busload::BAND_LOW;
uint32_t      g_pendingSinceMs = 0;
bool          g_hasPending = false;

// The band that a load qualifies for, from the band in force. A load that sits
// inside a dead band qualifies for the band in force, thus nothing moves.
busload::Band qualify(uint32_t pct, busload::Band current) {
  busload::Band b = current;

  if (b == busload::BAND_LOW   && pct >= RISE_TO_MID)  b = busload::BAND_MID;
  if (b == busload::BAND_MID   && pct >= RISE_TO_HIGH) b = busload::BAND_HIGH;
  if (b == busload::BAND_HIGH  && pct >= PULSE_EDGE)   b = busload::BAND_PULSE;

  if (b == busload::BAND_PULSE && pct <  PULSE_EDGE)   b = busload::BAND_HIGH;
  if (b == busload::BAND_HIGH  && pct <= FALL_TO_MID)  b = busload::BAND_MID;
  if (b == busload::BAND_MID   && pct <= FALL_TO_LOW)  b = busload::BAND_LOW;

  return b;
}

}  // namespace

namespace busload {

uint32_t percent(uint32_t framesPerSecond, uint32_t bitrate) {
  if (bitrate == 0) return 0;
  // 64-bit intermediate: a 1 Mbit/s bus at full load gives about 9000 frames
  // each second, and the product leaves the 32-bit range with a small margin.
  uint64_t bits = (uint64_t)framesPerSecond * CANTICK_LOAD_FRAME_BITS * 100u;
  return (uint32_t)(bits / bitrate);
}

void reset() {
  g_band = BAND_LOW;
  g_pending = BAND_LOW;
  g_pendingSinceMs = 0;
  g_hasPending = false;
}

Band update(uint32_t loadPercent, uint32_t nowMs) {
  const Band target = qualify(loadPercent, g_band);

  if (target == g_band) {
    g_hasPending = false;           // back inside the band, thus the change dies
    return g_band;
  }

  if (!g_hasPending || g_pending != target) {
    g_hasPending = true;
    g_pending = target;
    g_pendingSinceMs = nowMs;       // a new target starts the dwell again
  }

  // Unsigned arithmetic, thus the millisecond counter can wrap without a fault.
  if (nowMs - g_pendingSinceMs >= CANTICK_LOAD_DWELL_MS) {
    g_band = target;
    g_hasPending = false;
  }

  return g_band;
}

Band band() { return g_band; }

}  // namespace busload
