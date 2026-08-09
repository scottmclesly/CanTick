#pragma once
// ── Panel scheduler ──────────────────────────────────────────────────────────
// One tick, one frame, one driver call. DISPLAY.md §3 rule 14 fixes the tick at
// 20 Hz, and it forbids a driver call when the frame does not change.
//
// §2: a card takes all 6 rows, and both strips pause while it scrolls. A paused
// strip holds its slots. It does not reset, thus a card never blanks the bus
// picture that it interrupted.
//
// This unit is pure. The one hardware call sits behind the Driver seam below.

#include <stdint.h>
#include "config.h"
#include "bus_load.h"
#include "cards.h"
#include "status_led.h"

namespace panel {

// The only path to hardware. `wire` holds CANTICK_MATRIX_PIXELS colors in the
// order the LEDs sit on the data line. The host test installs a fake. The board
// installs the NeoPixel write, and that is its single call site.
//
// The driver owns the global brightness cap (DISPLAY.md §3 rule 16). The frame
// buffer keeps true color, thus the cap never hides a frame change.
using Driver = void (*)(const uint32_t *wire, int count);

// Install the driver and clear the panel state.
void begin(Driver driver);

// Clear the panel state and keep the driver.
void reset();

// Raise a card. It enters the queue under the DISPLAY.md §6 rules.
void raise(const cards::Card &c);

// Traffic, forwarded to the strips. One arrow is one message.
void noteRx(uint32_t frames);
void noteTx(uint32_t frames);
void noteDrop(uint32_t count);
void noteTxFail(uint32_t count);

// One 20 Hz tick. It advances the animation, renders one frame, and calls the
// driver only when the frame changed.
void tick(busload::Band band, uint32_t loadPercent, uint32_t busColor);

// True while a card holds the panel.
bool cardRunning();

// True when no card runs and none waits. The boot splash ticks until this is
// true, thus the splash finishes before the WiFi start (DISPLAY.md §7).
bool idle();

// ── Status pixel (DISPLAY.md §10) ────────────────────────────────────────────
// The C6 has no usable onboard LED, thus the matrix is the status output there.
// The pixel reproduces the LED literally: one pixel at (9, 5), a color for the
// state, and the blink phase that the status_led seam already gives. It is not
// a card, and it draws no word, thus it invents no vocabulary.
//
// Card text holds rows 0 to 4, thus the pixel never collides with a card. The
// panel draws it last, on top of a card or a strip, thus nothing hides it.

// The color for a state. The fault latch wins over the state.
uint32_t statusColor(led::State s, bool fault);

// The status_led output backend for the C6. Install it with led::setOutput().
// The S3 keeps its onboard LED, thus that variant never installs this and the
// panel draws no status pixel.
void statusPixelBackend(led::State s, bool fault, bool on);

// True once a backend has claimed the pixel.
bool statusPixelActive();

}  // namespace panel
