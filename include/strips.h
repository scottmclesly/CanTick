#pragma once
// ── Idle layout: the two traffic strips ──────────────────────────────────────
// DISPLAY.md §2 gives the layout, §3 rules 5 to 13 give the behavior, and §4
// gives the step rates and the shade pairs.
//
// A strip is a shift register of 10 slots. One slot is one column, and it fills
// the three rows of its strip. One arrow is one message (§3 rule 7), thus the
// arrow density follows the message rate with no separate density rule.
//
// This unit is pure. It draws into the matrix frame buffer, and it calls no
// driver.

#include <stdint.h>
#include "config.h"
#include "bus_load.h"

namespace strips {

// §2: the inbound strip is rows 0 to 2, and the outbound strip is rows 3 to 5.
// §3 rule 6: the inbound strip runs right to left, and the outbound strip runs
// left to right.
enum Which : uint8_t { INBOUND = 0, OUTBOUND = 1 };

enum Slot : uint8_t {
  EMPTY   = 0,   // no message, thus dark
  ARROW_A = 1,   // shade A of the bus-speed color
  ARROW_B = 2,   // shade B
  TOOTH   = 3,   // a message that failed: black, where the arrow goes
};

void reset();

// One arrow is one message. Count what arrived since the last step.
void noteRx(uint32_t frames);
void noteTx(uint32_t frames);

// §4: an increase of the drop counter blanks one slot on the inbound strip, and
// a failed canlink::send() blanks one slot on the outbound strip.
void noteDrop(uint32_t count);
void noteTxFail(uint32_t count);

// Advance one 20 Hz tick. The band picks the step rate and the shade pair, and
// the load percent drives the pulse rate. Gives true when the picture changed,
// thus the caller calls the driver only on a change (§3 rule 14).
bool tick(busload::Band band, uint32_t loadPercent);

// Read one slot. x runs 0 to 9.
Slot slot(Which which, int x);

// Scale a 0xRRGGBB color to a percent, one channel at a time.
uint32_t scale(uint32_t rgb, uint32_t pct);

// The pulse level now, as a percent of the card color. §4: the pulse swings
// from 100 % down to 25 %, at 1 Hz on 95 % load and 4 Hz on 100 % load.
uint32_t pulseLevel();

// Draw both strips into the matrix frame buffer. It writes the six strip rows
// and nothing else.
void render(uint32_t busColor);

}  // namespace strips
