#include "bus_load.h"

namespace {

// Threshold edges, from DISPLAY.md §3 rule 15. A threshold carries a 5 % band on
// each side, thus the dead band around 50 % runs from 45 % to 55 %.
constexpr uint32_t RISE_TO_MID   = CANTICK_LOAD_BAND_MID_PCT   + CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t FALL_TO_LOW   = CANTICK_LOAD_BAND_MID_PCT   - CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t RISE_TO_HIGH  = CANTICK_LOAD_BAND_HIGH_PCT  + CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t FALL_TO_MID   = CANTICK_LOAD_BAND_HIGH_PCT  - CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t RISE_TO_PULSE = CANTICK_LOAD_BAND_PULSE_PCT + CANTICK_LOAD_HYSTERESIS_PCT;
constexpr uint32_t FALL_TO_HIGH  = CANTICK_LOAD_BAND_PULSE_PCT - CANTICK_LOAD_HYSTERESIS_PCT;

busload::Band g_band = busload::LOW;
busload::Band g_pending = busload::LOW;
uint32_t      g_pendingSinceMs = 0;
bool          g_hasPending = false;

// The band that a load qualifies for, from the band in force. A load that sits
// inside a dead band qualifies for the band in force, thus nothing moves.
busload::Band qualify(uint32_t pct, busload::Band current) {
  busload::Band b = current;

  if (b == busload::LOW   && pct >= RISE_TO_MID)   b = busload::MID;
  if (b == busload::MID   && pct >= RISE_TO_HIGH)  b = busload::HIGH;
  if (b == busload::HIGH  && pct >= RISE_TO_PULSE) b = busload::PULSE;

  if (b == busload::PULSE && pct <= FALL_TO_HIGH)  b = busload::HIGH;
  if (b == busload::HIGH  && pct <= FALL_TO_MID)   b = busload::MID;
  if (b == busload::MID   && pct <= FALL_TO_LOW)   b = busload::LOW;

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
  g_band = LOW;
  g_pending = LOW;
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
