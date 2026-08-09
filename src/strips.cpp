#include "strips.h"
#include "matrix.h"

namespace {

constexpr int COLS = CANTICK_MATRIX_COLS;

// The decay behind the head, as a percent of the bus-speed color (DISPLAY.md
// §4). The head itself is CANTICK_DECAY_HEAD_PCT.
const uint8_t DECAY_PCT[CANTICK_DECAY_COLUMNS] = {
  CANTICK_DECAY_1_PCT, CANTICK_DECAY_2_PCT, CANTICK_DECAY_3_PCT,
};

strips::Slot g_slot[2][COLS];
uint8_t      g_markRow[2][COLS];   // the row that the mark in this column took
int          g_trailLeft[2];       // decay columns still to lay for this mark
uint32_t     g_pending[2];         // messages waiting for a slot
uint32_t     g_blank[2];           // failed messages waiting for a slot
uint32_t     g_stepTick = 0;

// A small xorshift for the row pick. It is deterministic, thus the host test
// repeats, and it never leaves this file.
uint32_t g_rng = 0x9E3779B9u;
uint32_t nextRand() {
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
  return g_rng;
}

// §3 rule 6. The inbound strip moves toward column 0, thus a new mark enters at
// column 9. The outbound strip moves the other way.
int entryColumn(int which) { return which == strips::INBOUND ? COLS - 1 : 0; }

// Which decay column a slot holds, or -1 when it holds none.
int trailIndex(strips::Slot s) {
  if (s < strips::TAIL || s > strips::TAIL_3) return -1;
  return (int)s - (int)strips::TAIL;
}

// Shift one strip by one pixel and fill the slot that opens.
bool step(int which) {
  strips::Slot before[COLS];
  for (int x = 0; x < COLS; x++) before[x] = g_slot[which][x];

  if (which == strips::INBOUND)
    for (int x = 0; x < COLS - 1; x++) {
      g_slot[which][x] = g_slot[which][x + 1];
      g_markRow[which][x] = g_markRow[which][x + 1];
    }
  else
    for (int x = COLS - 1; x > 0; x--) {
      g_slot[which][x] = g_slot[which][x - 1];
      g_markRow[which][x] = g_markRow[which][x - 1];
    }

  // The entry column is always behind the columns that went before it, thus the
  // head laid down first and the decay laid down next trails the head. That
  // holds on both strips with no direction test.
  const int e = entryColumn(which);
  if (g_blank[which] > 0) {
    // §3 rule 10: the failed message takes the place the mark would have had.
    g_blank[which]--;
    if (g_pending[which] > 0) g_pending[which]--;
    g_trailLeft[which] = 0;                   // a failed mark grows no decay
    g_slot[which][e] = strips::TOOTH;
  } else if (g_trailLeft[which] > 0) {
    const int idx = CANTICK_DECAY_COLUMNS - g_trailLeft[which];
    g_trailLeft[which]--;
    g_slot[which][e] = (strips::Slot)(strips::TAIL + idx);
  } else if (g_pending[which] > 0) {
    g_pending[which]--;
    g_trailLeft[which] = CANTICK_DECAY_COLUMNS;
    // §4: a message picks one of its strip's 2 rows at random, and the decay
    // inherits it.
    g_markRow[which][e] = (uint8_t)(nextRand() % CANTICK_STRIP_ROWS);
    g_slot[which][e] = strips::HEAD;
  } else {
    // §4: an idle strip with no traffic is dark. No placeholder mark.
    g_slot[which][e] = strips::EMPTY;
  }

  for (int x = 0; x < COLS; x++)
    if (before[x] != g_slot[which][x]) return true;
  return false;
}

}  // namespace

namespace strips {

void reset() {
  for (int w = 0; w < 2; w++) {
    for (int x = 0; x < COLS; x++) {
      g_slot[w][x] = EMPTY;
      g_markRow[w][x] = 0;
    }
    g_trailLeft[w] = 0;
    g_pending[w] = 0;
    g_blank[w] = 0;
  }
  g_stepTick = 0;
  g_rng = 0x9E3779B9u;
}

void noteRx(uint32_t frames)    { g_pending[INBOUND]  += frames; }
void noteTx(uint32_t frames)    { g_pending[OUTBOUND] += frames; }
void noteDrop(uint32_t count)   { g_blank[INBOUND]    += count; }
void noteTxFail(uint32_t count) { g_blank[OUTBOUND]   += count; }

bool tick(busload::Band band) {
  // §4: one pixel each 4 ticks below 50 % load, one pixel each tick above it.
  // That, and the mark density, are the only things the band changes.
  const uint32_t interval = (band == busload::BAND_LOW)
      ? CANTICK_STRIP_STEP_TICKS_SLOW
      : CANTICK_STRIP_STEP_TICKS_FAST;

  bool changed = false;
  if (++g_stepTick >= interval) {
    g_stepTick = 0;
    if (step(INBOUND))  changed = true;
    if (step(OUTBOUND)) changed = true;
  }
  return changed;
}

Slot slot(Which which, int x) {
  if (x < 0 || x >= COLS) return EMPTY;
  return g_slot[which][x];
}

int markRow(Which which, int x) {
  if (x < 0 || x >= COLS) return 0;
  return g_markRow[which][x];
}

uint32_t scale(uint32_t rgb, uint32_t pct) {
  const uint32_t r = ((rgb >> 16) & 0xFFu) * pct / 100u;
  const uint32_t g = ((rgb >> 8)  & 0xFFu) * pct / 100u;
  const uint32_t b = (rgb         & 0xFFu) * pct / 100u;
  return (r << 16) | (g << 8) | b;
}

void render(uint32_t busColor) {
  // §2: the idle layout owns rows 0-1 and 3-4. Row 2 is the unlit divider and
  // row 5 stays dark, thus blank the panel and put back only the marks.
  for (int x = 0; x < COLS; x++)
    for (int r = 0; r < CANTICK_MATRIX_ROWS; r++) matrix::setPixel(x, r, 0);

  for (int w = 0; w < 2; w++) {
    const int row0 = (w == INBOUND) ? CANTICK_STRIP_IN_ROW0
                                    : CANTICK_STRIP_OUT_ROW0;
    for (int x = 0; x < COLS; x++) {
      const Slot s = g_slot[w][x];
      if (s == EMPTY || s == TOOTH) continue;   // §3 rule 10: a tooth is black

      const int t = trailIndex(s);
      const uint32_t pct = (s == HEAD) ? CANTICK_DECAY_HEAD_PCT : DECAY_PCT[t];
      matrix::setPixel(x, row0 + g_markRow[w][x], scale(busColor, pct));
    }
  }
}

}  // namespace strips
