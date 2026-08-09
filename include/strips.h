#pragma once
// ── Idle layout: the two traffic strips ──────────────────────────────────────
// DISPLAY.md §2 gives the layout and §3 rules 5 to 10 give the behavior.
//
// A strip is a shift register of 10 slots, and one slot is one column. One
// message is one mark. A mark is a single pixel with a 3-column decay behind
// it, and it picks one of its strip's 2 rows at random. The decay stays on that
// row, thus a mark never smears across both.
//
// §3 rule 9: no band changes a shade. The decay is identical at every load, and
// the load rides on mark density and step rate alone.
//
// This unit is pure. It draws into the matrix frame buffer, and it calls no
// driver.

#include <stdint.h>
#include "config.h"
#include "bus_load.h"

namespace strips {

// §2: the inbound strip is rows 0 and 1, and the outbound strip is rows 3 and
// 4. Row 2 is the unlit divider and row 5 stays dark.
// §3 rule 6: the inbound strip runs right to left, the outbound left to right.
enum Which : uint8_t { INBOUND = 0, OUTBOUND = 1 };

enum Slot : uint8_t {
  EMPTY  = 0,   // no message, thus dark
  HEAD   = 1,   // the mark itself, at full shade
  TAIL   = 2,   // the first decay column
  TAIL_2 = 3,
  TAIL_3 = 4,
  TOOTH  = 5,   // a message that failed: black, where the mark goes
};

void reset();

// One mark is one message. Count what arrived since the last step.
void noteRx(uint32_t frames);
void noteTx(uint32_t frames);

// §4: an increase of the drop counter blanks one slot on the inbound strip, and
// a failed canlink::send() blanks one slot on the outbound strip.
void noteDrop(uint32_t count);
void noteTxFail(uint32_t count);

// Advance one 20 Hz tick. The band picks the step rate, and nothing else. Gives
// true when the picture changed, thus the caller calls the driver only on a
// change (§3 rule 11).
bool tick(busload::Band band);

// Read one slot, and the row that its mark took. x runs 0 to 9.
Slot slot(Which which, int x);
int  markRow(Which which, int x);

// Scale a 0xRRGGBB color to a percent, one channel at a time.
uint32_t scale(uint32_t rgb, uint32_t pct);

// Draw the idle layout into the matrix frame buffer. It writes every row: the
// two strips, the unlit divider and the dark row 5.
void render(uint32_t busColor);

}  // namespace strips
