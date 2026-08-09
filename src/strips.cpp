#include "strips.h"
#include "matrix.h"

namespace {

constexpr int COLS = CANTICK_MATRIX_COLS;

strips::Slot g_slot[2][COLS];
bool         g_shadeB[2];          // the shade that the next arrow takes
uint32_t     g_pending[2];         // messages waiting for a slot
uint32_t     g_blank[2];           // failed messages waiting for a slot

busload::Band g_band = busload::LOW;
uint32_t      g_pct = 0;
uint32_t      g_stepTick = 0;
uint32_t      g_pulseTick = 0;

// §3 rule 6. The inbound strip moves toward column 0, thus a new arrow enters
// at column 9. The outbound strip moves the other way.
int entryColumn(int which) { return which == strips::INBOUND ? COLS - 1 : 0; }

// Shift one strip by one pixel and fill the slot that opens.
bool step(int which) {
  strips::Slot before[COLS];
  for (int x = 0; x < COLS; x++) before[x] = g_slot[which][x];

  if (which == strips::INBOUND)
    for (int x = 0; x < COLS - 1; x++) g_slot[which][x] = g_slot[which][x + 1];
  else
    for (int x = COLS - 1; x > 0; x--) g_slot[which][x] = g_slot[which][x - 1];

  const int e = entryColumn(which);
  if (g_blank[which] > 0) {
    // §3 rule 13: the failed message takes the place the arrow would have had.
    g_blank[which]--;
    if (g_pending[which] > 0) g_pending[which]--;
    g_slot[which][e] = strips::TOOTH;
  } else if (g_pending[which] > 0) {
    g_pending[which]--;
    g_slot[which][e] = g_shadeB[which] ? strips::ARROW_B : strips::ARROW_A;
    g_shadeB[which] = !g_shadeB[which];
  } else {
    // §4: an idle strip with no traffic is dark. No placeholder arrow.
    g_slot[which][e] = strips::EMPTY;
  }

  for (int x = 0; x < COLS; x++)
    if (before[x] != g_slot[which][x]) return true;
  return false;
}

// §4 shade pairs, as a percent of the bus-speed color.
uint32_t shadeA() { return g_band == busload::HIGH ? CANTICK_SHADE_A_HIGH_PCT : CANTICK_SHADE_A_PCT; }
uint32_t shadeB() { return g_band == busload::HIGH ? CANTICK_SHADE_B_HIGH_PCT : CANTICK_SHADE_B_PCT; }

// §4: 1 Hz at 95 % load, rising to 4 Hz at 100 % load.
uint32_t pulseHz() {
  if (g_pct <= CANTICK_LOAD_BAND_PULSE_PCT) return CANTICK_PULSE_MIN_HZ;
  if (g_pct >= 100) return CANTICK_PULSE_MAX_HZ;
  const uint32_t span = 100u - CANTICK_LOAD_BAND_PULSE_PCT;
  const uint32_t rise = CANTICK_PULSE_MAX_HZ - CANTICK_PULSE_MIN_HZ;
  return CANTICK_PULSE_MIN_HZ + (g_pct - CANTICK_LOAD_BAND_PULSE_PCT) * rise / span;
}

}  // namespace

namespace strips {

void reset() {
  for (int w = 0; w < 2; w++) {
    for (int x = 0; x < COLS; x++) g_slot[w][x] = EMPTY;
    g_shadeB[w] = false;
    g_pending[w] = 0;
    g_blank[w] = 0;
  }
  g_band = busload::LOW;
  g_pct = 0;
  g_stepTick = 0;
  g_pulseTick = 0;
}

void noteRx(uint32_t frames)    { g_pending[INBOUND]  += frames; }
void noteTx(uint32_t frames)    { g_pending[OUTBOUND] += frames; }
void noteDrop(uint32_t count)   { g_blank[INBOUND]    += count; }
void noteTxFail(uint32_t count) { g_blank[OUTBOUND]   += count; }

bool tick(busload::Band band, uint32_t loadPercent) {
  g_band = band;
  g_pct = loadPercent;

  bool changed = false;

  // §4: one pixel each 4 ticks below 50 % load, one pixel each tick above it.
  const uint32_t interval = (band == busload::LOW)
      ? CANTICK_STRIP_STEP_TICKS_SLOW
      : CANTICK_STRIP_STEP_TICKS_FAST;

  if (++g_stepTick >= interval) {
    g_stepTick = 0;
    if (step(INBOUND))  changed = true;
    if (step(OUTBOUND)) changed = true;
  }

  // The pulse repaints the strip on every tick, thus the frame changes.
  if (band == busload::PULSE) {
    g_pulseTick++;
    changed = true;
  }

  return changed;
}

Slot slot(Which which, int x) {
  if (x < 0 || x >= COLS) return EMPTY;
  return g_slot[which][x];
}

uint32_t scale(uint32_t rgb, uint32_t pct) {
  const uint32_t r = ((rgb >> 16) & 0xFFu) * pct / 100u;
  const uint32_t g = ((rgb >> 8)  & 0xFFu) * pct / 100u;
  const uint32_t b = (rgb         & 0xFFu) * pct / 100u;
  return (r << 16) | (g << 8) | b;
}

uint32_t pulseLevel() {
  uint32_t period = CANTICK_MATRIX_TICK_HZ / pulseHz();
  if (period < 2) period = 2;
  const uint32_t half = period / 2;
  const uint32_t p = g_pulseTick % period;
  const uint32_t span = CANTICK_PULSE_MAX_PCT - CANTICK_PULSE_MIN_PCT;

  if (p < half) return CANTICK_PULSE_MAX_PCT - (span * p / half);
  return CANTICK_PULSE_MIN_PCT + (span * (p - half) / (period - half));
}

void render(uint32_t busColor) {
  // §3 rule 12: above the pulse edge the strip is a solid line that pulses. A
  // failed message stays black, thus a missing tooth still reads (rule 13).
  const bool solid = (g_band == busload::PULSE);
  const uint32_t solidColor = solid ? scale(busColor, pulseLevel()) : 0;

  for (int w = 0; w < 2; w++) {
    const int row0 = (w == INBOUND) ? CANTICK_STRIP_IN_ROW0 : CANTICK_STRIP_OUT_ROW0;

    for (int x = 0; x < COLS; x++) {
      uint32_t color = 0;
      const Slot s = g_slot[w][x];

      if (s == TOOTH) {
        color = 0;
      } else if (solid) {
        color = solidColor;
      } else if (s == ARROW_A) {
        color = scale(busColor, shadeA());
      } else if (s == ARROW_B) {
        color = scale(busColor, shadeB());
      }

      for (int r = 0; r < CANTICK_STRIP_ROWS; r++)
        matrix::setPixel(x, row0 + r, color);
    }
  }
}

}  // namespace strips
