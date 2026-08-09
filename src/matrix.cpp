#include "matrix.h"
#include "config.h"

namespace {

// The ported code below reads these two names. Bind them to the DISPLAY.md §1
// geometry in config.h. The values are the same, thus the ported function body
// stays byte-identical to reference/CANtick_feedback.ino.
constexpr int MATRIX_WIDTH  = CANTICK_MATRIX_COLS;
constexpr int MATRIX_HEIGHT = CANTICK_MATRIX_ROWS;

// Frame buffer in panel coordinates. It is plain memory, and no hardware call
// touches it.
uint32_t g_fb[CANTICK_MATRIX_ROWS][CANTICK_MATRIX_COLS];

// ── Ported from reference/CANtick_feedback.ino, unchanged ────────────────────
// 3x5 font, column encoded, bit 0 top.
const uint8_t FONT_DIGITS[10][3] = {
  {31,17,31},{18,31,16},{25,21,18},{17,21,31},{7,4,31},
  {23,21,9},{31,21,29},{1,29,3},{31,21,31},{23,21,31}
};
const uint8_t FONT_ALPHA[26][3] = {
  {30,5,30},{31,21,10},{14,17,17},{31,17,14},{31,21,17},{31,5,1},{14,17,29},{31,4,31},
  {17,31,17},{8,16,15},{31,10,17},{31,16,16},{31,2,31},{23,10,29},{14,17,14},{31,5,2},
  {14,17,30},{31,5,26},{18,21,9},{1,31,1},{15,16,15},{7,24,7},{31,8,31},{27,4,27},
  {3,28,3},{25,21,19}
};

}  // namespace

namespace matrix {

// ── Ported from reference/CANtick_feedback.ino, unchanged ────────────────────
// SAFETY of the picture, not of the bus: this mapping is bench-proven on the
// mounted enclosure. DISPLAY.md §1 locks it. Do not derive it again.
int xyToIndex(int x, int y, uint8_t preset)
{
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return -1;

  const bool flipX = preset & 0x01;
  const bool flipY = preset & 0x02;
  const bool zigzag = preset & 0x04;
  const bool columnMajor = preset & 0x08;

  if (flipX) x = MATRIX_WIDTH - 1 - x;
  if (flipY) y = MATRIX_HEIGHT - 1 - y;

  if (!columnMajor)
  {
    if (zigzag && (y & 1)) x = MATRIX_WIDTH - 1 - x;
    return y * MATRIX_WIDTH + x;
  }
  if (zigzag && (x & 1)) y = MATRIX_HEIGHT - 1 - y;
  return x * MATRIX_HEIGHT + y;
}

// ── Ported from reference/CANtick_feedback.ino, unchanged ────────────────────
void glyph(char ch, uint8_t o[3])
{
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch >= '0' && ch <= '9')      { o[0]=FONT_DIGITS[ch-'0'][0]; o[1]=FONT_DIGITS[ch-'0'][1]; o[2]=FONT_DIGITS[ch-'0'][2]; }
  else if (ch >= 'A' && ch <= 'Z') { o[0]=FONT_ALPHA[ch-'A'][0]; o[1]=FONT_ALPHA[ch-'A'][1]; o[2]=FONT_ALPHA[ch-'A'][2]; }
  else { o[0]=o[1]=o[2]=0; }
}

// ── Frame buffer ─────────────────────────────────────────────────────────────

void clear()
{
  for (int y = 0; y < MATRIX_HEIGHT; y++)
    for (int x = 0; x < MATRIX_WIDTH; x++)
      g_fb[y][x] = 0;
}

void setPixel(int x, int y, uint32_t color)
{
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return;
  g_fb[y][x] = color;
}

uint32_t pixel(int x, int y)
{
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return 0;
  return g_fb[y][x];
}

void drawText(const char *s, int ox, int oy, uint32_t color)
{
  int x = ox;
  while (*s)
  {
    uint8_t g[3];
    glyph(*s, g);
    for (int gx = 0; gx < CANTICK_FONT_WIDTH; gx++)
      for (int gy = 0; gy < CANTICK_FONT_HEIGHT; gy++)
        if (g[gx] & (1 << gy)) setPixel(x + gx, oy + gy, color);
    x += CANTICK_GLYPH_ADVANCE;
    s++;
  }
}

int textWidth(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n <= 0 ? 0 : n * CANTICK_GLYPH_ADVANCE - 1;
}

void toWire(uint32_t *out) {
  for (int i = 0; i < CANTICK_MATRIX_PIXELS; i++) out[i] = 0;

  for (int y = 0; y < MATRIX_HEIGHT; y++) {
    for (int x = 0; x < MATRIX_WIDTH; x++) {
      const int i = xyToIndex(x, y, CANTICK_MATRIX_PRESET);
      if (i >= 0) out[i] = g_fb[y][x];
    }
  }
}

}  // namespace matrix
