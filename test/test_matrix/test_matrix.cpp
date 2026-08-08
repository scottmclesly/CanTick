// Host tests for the front panel rendering primitives (Matrix-TODO.md Phase 2).
//
// ── The oracle comes from DISPLAY.md §1, not from src/matrix.cpp ─────────────
//
// DISPLAY.md §1 locks three facts about the mounted panel:
//
//   Geometry            10 columns x 6 rows
//   Wiring order        column-major
//   Orientation preset  LOCKED_PRESET = 10, flipY plus column-major, no zigzag
//
// Derive the wire index from those words alone:
//
//   Column-major, no zigzag: the wire runs whole columns in order, and each
//   column runs in one direction. Thus column x starts at wire index x * 6.
//   flipY: the row count runs from the bottom. Thus row y sits at offset
//   (6 - 1 - y) inside its column.
//   The preset does not name flipX. Thus the column order is not reversed.
//
//                       index = x * 6 + (5 - y)
//
// Every expected index below comes from that formula. The formula came from the
// three locked facts, before a line of src/matrix.cpp was read.
//
// DISPLAY.md §1 gives the font as "3x5, column-encoded". It gives no glyph
// table, thus a glyph oracle cannot come from it. The expected pixel set for
// the letter H below is the shape of an H on a 3x5 grid, written as a picture
// before the font bytes were read.

#include <unity.h>
#include "matrix.h"

namespace {

// DISPLAY.md §1 locks this preset for the mounted enclosure.
constexpr uint8_t PRESET = CANTICK_MATRIX_PRESET;

constexpr uint32_t INK = 0x00FF00;

}  // namespace

void setUp(void) { matrix::clear(); }
void tearDown(void) {}

// ── xyToIndex against preset 10 ──────────────────────────────────────────────

// index = x * 6 + (5 - y)
void test_four_corners(void) {
  TEST_ASSERT_EQUAL_INT(5,  matrix::xyToIndex(0, 0, PRESET));   // 0*6 + 5
  TEST_ASSERT_EQUAL_INT(59, matrix::xyToIndex(9, 0, PRESET));   // 9*6 + 5
  TEST_ASSERT_EQUAL_INT(0,  matrix::xyToIndex(0, 5, PRESET));   // 0*6 + 0
  TEST_ASSERT_EQUAL_INT(54, matrix::xyToIndex(9, 5, PRESET));   // 9*6 + 0
}

void test_first_column_both_ends(void) {
  TEST_ASSERT_EQUAL_INT(5, matrix::xyToIndex(0, 0, PRESET));    // top of column 0
  TEST_ASSERT_EQUAL_INT(0, matrix::xyToIndex(0, 5, PRESET));    // bottom of column 0
  TEST_ASSERT_EQUAL_INT(2, matrix::xyToIndex(0, 3, PRESET));    // 0*6 + 2
}

void test_last_column_both_ends(void) {
  TEST_ASSERT_EQUAL_INT(59, matrix::xyToIndex(9, 0, PRESET));   // top of column 9
  TEST_ASSERT_EQUAL_INT(54, matrix::xyToIndex(9, 5, PRESET));   // bottom of column 9
  TEST_ASSERT_EQUAL_INT(56, matrix::xyToIndex(9, 3, PRESET));   // 9*6 + 2
}

void test_mid_panel_coordinate(void) {
  TEST_ASSERT_EQUAL_INT(27, matrix::xyToIndex(4, 2, PRESET));   // 4*6 + 3
}

void test_off_panel_gives_minus_one(void) {
  TEST_ASSERT_EQUAL_INT(-1, matrix::xyToIndex(-1, 0, PRESET));
  TEST_ASSERT_EQUAL_INT(-1, matrix::xyToIndex(0, -1, PRESET));
  TEST_ASSERT_EQUAL_INT(-1, matrix::xyToIndex(10, 0, PRESET));
  TEST_ASSERT_EQUAL_INT(-1, matrix::xyToIndex(0, 6, PRESET));
}

// Column-major with no zigzag covers each LED one time. A map that collides or
// skips is wrong, whatever a single coordinate says.
void test_map_covers_every_led_once(void) {
  bool seen[CANTICK_MATRIX_PIXELS] = {false};
  for (int x = 0; x < matrix::WIDTH; x++) {
    for (int y = 0; y < matrix::HEIGHT; y++) {
      int i = matrix::xyToIndex(x, y, PRESET);
      TEST_ASSERT_TRUE(i >= 0 && i < CANTICK_MATRIX_PIXELS);
      TEST_ASSERT_FALSE_MESSAGE(seen[i], "two coordinates map to one LED");
      seen[i] = true;
    }
  }
  for (int i = 0; i < CANTICK_MATRIX_PIXELS; i++)
    TEST_ASSERT_TRUE_MESSAGE(seen[i], "an LED has no coordinate");
}

// ── Font and frame buffer ────────────────────────────────────────────────────

// The letter H on a 3x5 grid. This picture is the oracle.
static const char *H_PICTURE[CANTICK_FONT_HEIGHT] = {
  "X.X",
  "X.X",
  "XXX",
  "X.X",
  "X.X",
};

void test_glyph_H_lands_on_expected_pixels(void) {
  matrix::drawText("H", 0, 0, INK);

  for (int y = 0; y < matrix::HEIGHT; y++) {
    for (int x = 0; x < matrix::WIDTH; x++) {
      bool lit = (y < CANTICK_FONT_HEIGHT && x < CANTICK_FONT_WIDTH)
                 && H_PICTURE[y][x] == 'X';
      TEST_ASSERT_EQUAL_HEX32(lit ? INK : 0u, matrix::pixel(x, y));
    }
  }
}

void test_second_glyph_advances_four_columns(void) {
  matrix::drawText("HH", 0, 0, INK);

  for (int y = 0; y < CANTICK_FONT_HEIGHT; y++) {
    for (int x = 0; x < CANTICK_FONT_WIDTH; x++) {
      uint32_t want = (H_PICTURE[y][x] == 'X') ? INK : 0u;
      TEST_ASSERT_EQUAL_HEX32(want, matrix::pixel(x, y));
      TEST_ASSERT_EQUAL_HEX32(want, matrix::pixel(x + 4, y));
    }
  }
  // The blank column between the two glyphs stays dark.
  for (int y = 0; y < CANTICK_FONT_HEIGHT; y++)
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(3, y));
}

void test_unknown_character_draws_nothing(void) {
  matrix::drawText("?", 0, 0, INK);
  for (int y = 0; y < matrix::HEIGHT; y++)
    for (int x = 0; x < matrix::WIDTH; x++)
      TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, y));
}

void test_lower_case_draws_the_same_glyph(void) {
  matrix::drawText("h", 0, 0, INK);
  for (int y = 0; y < CANTICK_FONT_HEIGHT; y++)
    for (int x = 0; x < CANTICK_FONT_WIDTH; x++)
      TEST_ASSERT_EQUAL_HEX32((H_PICTURE[y][x] == 'X') ? INK : 0u,
                              matrix::pixel(x, y));
}

// The H starts at column 8. Its left bar lands on column 8 and its middle bar
// on column 9. Its right bar falls on column 10, which is off the panel, thus
// the draw drops it. The panel then holds the H picture, clipped.
void test_draw_off_the_panel_is_dropped(void) {
  matrix::drawText("H", 8, 0, INK);

  for (int y = 0; y < matrix::HEIGHT; y++) {
    for (int x = 0; x < matrix::WIDTH; x++) {
      int gx = x - 8;
      bool lit = (y < CANTICK_FONT_HEIGHT && gx >= 0 && gx < CANTICK_FONT_WIDTH)
                 && H_PICTURE[y][gx] == 'X';
      TEST_ASSERT_EQUAL_HEX32(lit ? INK : 0u, matrix::pixel(x, y));
    }
  }
}

void test_clear_darkens_every_pixel(void) {
  matrix::drawText("H", 0, 0, INK);
  matrix::clear();
  for (int y = 0; y < matrix::HEIGHT; y++)
    for (int x = 0; x < matrix::WIDTH; x++)
      TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, y));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_four_corners);
  RUN_TEST(test_first_column_both_ends);
  RUN_TEST(test_last_column_both_ends);
  RUN_TEST(test_mid_panel_coordinate);
  RUN_TEST(test_off_panel_gives_minus_one);
  RUN_TEST(test_map_covers_every_led_once);
  RUN_TEST(test_glyph_H_lands_on_expected_pixels);
  RUN_TEST(test_second_glyph_advances_four_columns);
  RUN_TEST(test_unknown_character_draws_nothing);
  RUN_TEST(test_lower_case_draws_the_same_glyph);
  RUN_TEST(test_draw_off_the_panel_is_dropped);
  RUN_TEST(test_clear_darkens_every_pixel);
  return UNITY_END();
}
