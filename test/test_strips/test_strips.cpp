// Host tests for the idle layout (Matrix-TODO.md Phase 5).
//
// ── The oracle comes from DISPLAY.md §2, §3 and §4 ───────────────────────────
//
// §2   Rows 0-1 are the inbound strip. Row 2 is the unlit divider. Rows 3-4 are
//      the outbound strip. Row 5 stays dark.
// §3.6 The inbound strip runs right to left. The outbound strip runs left to
//      right.
// §3.7 One mark is one message.
// §3.9 Load is carried by mark density and step rate only. The mark's decay is
//      identical at every load. No band changes a shade.
// §3.10 A failed packet is a missing mark, in black, where the mark goes.
// §4   One mark is a single pixel with a 4-column decay: 100 %, then 50 %, 25 %
//      and 10 % of the bus-speed color.
// §4   A message picks one of its strip's 2 rows at random, and its decay stays
//      on that row.
// §4   Step rate: one pixel each 4 ticks below 50 % load, one pixel each tick
//      at 50 % and above.
//
// The head enters the strip first and the decay follows on later steps. The
// entry column is always behind the columns that went before it, thus the decay
// lands behind the head on both strips with no direction test in the code.

#include <unity.h>
#include "strips.h"
#include "matrix.h"

namespace {

// A color with a distinct value in each channel, thus a wrong shade shows.
constexpr uint32_t BUS = 0x804020;

// Every band except LOW steps one pixel each tick.
void tickFast(uint32_t times) {
  for (uint32_t i = 0; i < times; i++) strips::tick(busload::BAND_MID);
}

// The pixel of a mark, whichever of its strip's rows it took.
uint32_t markPixel(int row0, int x) {
  uint32_t v = 0;
  for (int r = 0; r < CANTICK_STRIP_ROWS; r++) v |= matrix::pixel(x, row0 + r);
  return v;
}

bool stripRowsAreDark(int row0) {
  for (int x = 0; x < matrix::WIDTH; x++)
    for (int r = 0; r < CANTICK_STRIP_ROWS; r++)
      if (matrix::pixel(x, row0 + r) != 0) return false;
  return true;
}

}  // namespace

void setUp(void) {
  strips::reset();
  matrix::clear();
}
void tearDown(void) {}

// ── An empty strip is dark ───────────────────────────────────────────────────

void test_an_empty_strip_is_dark(void) {
  tickFast(20);
  strips::render(BUS);

  TEST_ASSERT_TRUE(stripRowsAreDark(CANTICK_STRIP_IN_ROW0));
  TEST_ASSERT_TRUE(stripRowsAreDark(CANTICK_STRIP_OUT_ROW0));
}

void test_a_quiet_strip_reports_no_change(void) {
  tickFast(4);                                  // flush any start-up motion
  TEST_ASSERT_FALSE(strips::tick(busload::BAND_MID));
}

// ── Layout, DISPLAY.md §2 ────────────────────────────────────────────────────

void test_each_strip_owns_two_rows(void) {
  TEST_ASSERT_EQUAL_INT(2, CANTICK_STRIP_ROWS);
  TEST_ASSERT_EQUAL_INT(0, CANTICK_STRIP_IN_ROW0);
  TEST_ASSERT_EQUAL_INT(2, CANTICK_STRIP_DIVIDER_ROW);
  TEST_ASSERT_EQUAL_INT(3, CANTICK_STRIP_OUT_ROW0);
}

// The divider row is unlit and row 5 is dark, whatever the traffic.
void test_the_divider_row_and_row_5_stay_dark(void) {
  strips::noteRx(10);
  strips::noteTx(10);
  tickFast(10);
  strips::render(BUS);

  for (int x = 0; x < matrix::WIDTH; x++) {
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, CANTICK_STRIP_DIVIDER_ROW));
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, CANTICK_MATRIX_ROWS - 1));
  }
}

// ── Direction ────────────────────────────────────────────────────────────────

// §3 rule 6: the inbound strip runs right to left, thus a mark enters at column
// 9 and walks toward column 0. Its decay trails behind it, on the higher column.
void test_the_inbound_strip_runs_right_to_left(void) {
  strips::noteRx(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 9));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 8));
  TEST_ASSERT_EQUAL_INT(strips::TAIL, strips::slot(strips::INBOUND, 9));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD,   strips::slot(strips::INBOUND, 7));
  TEST_ASSERT_EQUAL_INT(strips::TAIL,   strips::slot(strips::INBOUND, 8));
  TEST_ASSERT_EQUAL_INT(strips::TAIL_2, strips::slot(strips::INBOUND, 9));
}

// §3 rule 6: the outbound strip runs left to right, thus the decay sits on the
// lower column. The decay is behind the head on both strips.
void test_the_outbound_strip_runs_left_to_right(void) {
  strips::noteTx(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::OUTBOUND, 0));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::OUTBOUND, 1));
  TEST_ASSERT_EQUAL_INT(strips::TAIL, strips::slot(strips::OUTBOUND, 0));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD,   strips::slot(strips::OUTBOUND, 2));
  TEST_ASSERT_EQUAL_INT(strips::TAIL,   strips::slot(strips::OUTBOUND, 1));
  TEST_ASSERT_EQUAL_INT(strips::TAIL_2, strips::slot(strips::OUTBOUND, 0));
}

// The head enters at column 9 and reaches column 0 after ten ticks. The decay
// is three columns long, thus the strip needs more ticks to clear.
void test_a_mark_leaves_the_far_end(void) {
  strips::noteRx(1);
  tickFast(10);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 0));

  tickFast(CANTICK_MATRIX_COLS + CANTICK_DECAY_COLUMNS);
  for (int x = 0; x < matrix::WIDTH; x++)
    TEST_ASSERT_EQUAL_INT(strips::EMPTY, strips::slot(strips::INBOUND, x));
}

// ── The mark and its decay, DISPLAY.md §4 ────────────────────────────────────

void test_a_mark_is_one_pixel_with_a_three_column_decay(void) {
  TEST_ASSERT_EQUAL_INT(4, CANTICK_MARK_COLUMNS);

  strips::noteRx(1);
  tickFast(4);                                  // head 6, decay 7, 8, 9
  strips::render(BUS);

  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, CANTICK_DECAY_HEAD_PCT), markPixel(0, 6));
  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, CANTICK_DECAY_1_PCT),    markPixel(0, 7));
  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, CANTICK_DECAY_2_PCT),    markPixel(0, 8));
  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, CANTICK_DECAY_3_PCT),    markPixel(0, 9));
}

// The decay is monotonic and never reaches zero, or it would not read as one.
void test_the_decay_falls_at_every_step(void) {
  TEST_ASSERT_TRUE(CANTICK_DECAY_HEAD_PCT > CANTICK_DECAY_1_PCT);
  TEST_ASSERT_TRUE(CANTICK_DECAY_1_PCT > CANTICK_DECAY_2_PCT);
  TEST_ASSERT_TRUE(CANTICK_DECAY_2_PCT > CANTICK_DECAY_3_PCT);
  TEST_ASSERT_TRUE(CANTICK_DECAY_3_PCT > 0);
}

// §4: the whole mark sits on one row of its strip, and the other row stays dark
// under it.
void test_a_mark_keeps_its_decay_on_one_row(void) {
  strips::noteRx(1);
  tickFast(4);
  strips::render(BUS);

  const int row = strips::markRow(strips::INBOUND, 6);
  const int other = 1 - row;
  for (int x = 6; x <= 9; x++) {
    TEST_ASSERT_NOT_EQUAL(0u, matrix::pixel(x, CANTICK_STRIP_IN_ROW0 + row));
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, CANTICK_STRIP_IN_ROW0 + other));
  }
}

// The row pick is random, thus a run of marks does not all take one row.
void test_the_row_pick_varies_across_marks(void) {
  bool seen[2] = {false, false};
  for (int i = 0; i < 40; i++) {
    strips::noteRx(1);
    tickFast(1);
    seen[strips::markRow(strips::INBOUND, 9) & 1] = true;
    tickFast(CANTICK_MARK_COLUMNS);
  }
  TEST_ASSERT_TRUE_MESSAGE(seen[0] && seen[1], "every mark took the same row");
}

// ── No band changes a shade, DISPLAY.md §3 rule 9 ────────────────────────────

// The whole point of the locked design: HIGH and PULSE paint exactly what MID
// paints. Only the density and the step rate move.
void test_the_mark_is_identical_in_every_band(void) {
  const busload::Band BANDS[4] = {
    busload::BAND_LOW, busload::BAND_MID, busload::BAND_HIGH, busload::BAND_PULSE,
  };
  uint32_t seen[4][4];

  for (int b = 0; b < 4; b++) {
    strips::reset();
    matrix::clear();
    strips::noteRx(1);
    // The bands differ in step rate, thus tick each one for the same number of
    // STEPS. Equal ticks would give the fast bands more steps.
    const uint32_t interval = (BANDS[b] == busload::BAND_LOW)
        ? CANTICK_STRIP_STEP_TICKS_SLOW : CANTICK_STRIP_STEP_TICKS_FAST;
    for (int s = 0; s < 4; s++)
      for (uint32_t t = 0; t < interval; t++) strips::tick(BANDS[b]);
    strips::render(BUS);
    for (int c = 0; c < 4; c++) seen[b][c] = markPixel(0, 6 + c);
  }

  for (int b = 1; b < 4; b++)
    for (int c = 0; c < 4; c++)
      TEST_ASSERT_EQUAL_HEX32_MESSAGE(seen[0][c], seen[b][c],
                                      "a band changed the mark");
}

// ── Step rate ────────────────────────────────────────────────────────────────

// §4: below 50 % load the strip steps one pixel each 4 ticks.
void test_the_slow_step_rate_below_50_percent(void) {
  strips::noteRx(1);
  strips::tick(busload::BAND_LOW);
  strips::tick(busload::BAND_LOW);
  strips::tick(busload::BAND_LOW);
  TEST_ASSERT_EQUAL_INT(strips::EMPTY, strips::slot(strips::INBOUND, 9));

  strips::tick(busload::BAND_LOW);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 9));
}

// §4: at 50 % and above the strip steps one pixel each tick.
void test_the_fast_step_rate_at_50_percent_and_above(void) {
  strips::noteRx(1);
  strips::tick(busload::BAND_MID);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 9));
}

// ── Missing tooth ────────────────────────────────────────────────────────────

// §4: an increase of the drop counter blanks one slot on the inbound strip.
void test_a_drop_blanks_a_slot_on_the_inbound_strip(void) {
  strips::noteRx(3);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 9));

  strips::noteDrop(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::TOOTH, strips::slot(strips::INBOUND, 9));

  // A failed mark grows no decay, thus the next slot starts a fresh mark.
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::HEAD, strips::slot(strips::INBOUND, 9));
}

// §4: a failed canlink::send() blanks one slot on the outbound strip.
void test_a_send_failure_blanks_a_slot_on_the_outbound_strip(void) {
  strips::noteTx(3);
  strips::noteTxFail(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::TOOTH, strips::slot(strips::OUTBOUND, 0));

  // The inbound strip is untouched by a send failure.
  TEST_ASSERT_EQUAL_INT(strips::EMPTY, strips::slot(strips::INBOUND, 9));
}

// §3 rule 10: the tooth is black, in the place where the mark goes.
void test_a_tooth_renders_black(void) {
  strips::noteRx(2);
  strips::noteDrop(1);
  tickFast(1);
  strips::render(BUS);

  for (int r = 0; r < CANTICK_STRIP_ROWS; r++)
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(9, CANTICK_STRIP_IN_ROW0 + r));
}

// ── Color helper ─────────────────────────────────────────────────────────────

void test_scale_works_one_channel_at_a_time(void) {
  TEST_ASSERT_EQUAL_HEX32(0x804020u, strips::scale(0x804020u, 100));
  TEST_ASSERT_EQUAL_HEX32(0x402010u, strips::scale(0x804020u, 50));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, strips::scale(0x804020u, 0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_strip_is_dark);
  RUN_TEST(test_a_quiet_strip_reports_no_change);
  RUN_TEST(test_each_strip_owns_two_rows);
  RUN_TEST(test_the_divider_row_and_row_5_stay_dark);
  RUN_TEST(test_the_inbound_strip_runs_right_to_left);
  RUN_TEST(test_the_outbound_strip_runs_left_to_right);
  RUN_TEST(test_a_mark_leaves_the_far_end);
  RUN_TEST(test_a_mark_is_one_pixel_with_a_three_column_decay);
  RUN_TEST(test_the_decay_falls_at_every_step);
  RUN_TEST(test_a_mark_keeps_its_decay_on_one_row);
  RUN_TEST(test_the_row_pick_varies_across_marks);
  RUN_TEST(test_the_mark_is_identical_in_every_band);
  RUN_TEST(test_the_slow_step_rate_below_50_percent);
  RUN_TEST(test_the_fast_step_rate_at_50_percent_and_above);
  RUN_TEST(test_a_drop_blanks_a_slot_on_the_inbound_strip);
  RUN_TEST(test_a_send_failure_blanks_a_slot_on_the_outbound_strip);
  RUN_TEST(test_a_tooth_renders_black);
  RUN_TEST(test_scale_works_one_channel_at_a_time);
  return UNITY_END();
}
