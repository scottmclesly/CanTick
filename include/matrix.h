#pragma once
// ── Front panel rendering primitives ─────────────────────────────────────────
// DISPLAY.md is the authority for the panel. Read it before you change a value.
//
// This unit is pure. It holds no hardware call, no NeoPixel type, no show() and
// no pin access. The frame buffer is plain memory. A driver reads the buffer in
// a later phase. Thus the host test build compiles this file.
//
// `xyToIndex` and the 3x5 font come from reference/CANtick_feedback.ino, the
// bench calibration sketch. DISPLAY.md §1 names them the canonical rendering
// primitives. Do not write a second index function.

#include <stdint.h>
#include "config.h"

namespace matrix {

// Panel geometry (DISPLAY.md §1).
constexpr int WIDTH  = CANTICK_MATRIX_COLS;
constexpr int HEIGHT = CANTICK_MATRIX_ROWS;

// Map a panel coordinate to a position on the wire. `preset` is an orientation
// map. DISPLAY.md §1 locks CANTICK_MATRIX_PRESET for the mounted enclosure.
// The function gives -1 for a coordinate off the panel.
int xyToIndex(int x, int y, uint8_t preset);

// Give the three column bytes of a character. Bit 0 is the top row. An unknown
// character gives three empty columns.
void glyph(char ch, uint8_t o[3]);

// ── Frame buffer ─────────────────────────────────────────────────────────────
// One 0xRRGGBB value per panel coordinate. The brightness cap applies later, in
// the driver.

void clear();
void setPixel(int x, int y, uint32_t color);
uint32_t pixel(int x, int y);

// Draw a string with the top-left corner of the first glyph at (ox, oy). A
// pixel off the panel is dropped.
void drawText(const char *s, int ox, int oy, uint32_t color);

}  // namespace matrix
