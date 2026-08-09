// Host tests for the idle layout (Matrix-TODO.md Phase 5).
//
// ── The oracle comes from DISPLAY.md §2, §3 and §4 ───────────────────────────
//
// §2:  The inbound strip is rows 0 to 2. The outbound strip is rows 3 to 5.
// §3.6 The inbound strip runs right to left. The outbound strip runs left to
//      right.
// §3.7 One arrow is one message.
// §4   An idle strip with no traffic is dark. Do not draw a placeholder arrow.
// §4   Step rate: one pixel each 4 ticks below 50 % load, one pixel each tick
//      at 50 % and above.
// §4   Shade pairs, as a fraction of the bus-speed color:
//          below 90 %      A 100 %   B 40 %
//          90 % to 95 %    A  25 %   B 15 %
//          95 % and above  a solid line that pulses, 100 % down to 25 %
// §4   A failed canlink::send() blanks one slot on the outbound strip. An
//      increase of the drop counter blanks one slot on the inbound strip.
//
// One point DISPLAY.md leaves open. It calls the moving mark an "arrow" and
// names a "missing arrow shape", but it gives no arrow glyph. The strip steps
// one pixel at a time and it is 10 columns wide, thus this build makes one
// arrow one column, filling the three rows of its strip. The tests below do
// not depend on that choice: they read slots and strip rows, not a glyph.

#include <unity.h>
#include "strips.h"
#include "matrix.h"

namespace {

// A color with a distinct value in each channel, thus a wrong shade shows.
constexpr uint32_t BUS = 0x804020;

// Every band except LOW steps one pixel each tick.
void tickFast(uint32_t times) {
  for (uint32_t i = 0; i < times; i++) strips::tick(busload::MID, 60);
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
  TEST_ASSERT_FALSE(strips::tick(busload::MID, 60));
}

// ── Direction ────────────────────────────────────────────────────────────────

// §3 rule 6: the inbound strip runs right to left, thus an arrow enters at
// column 9 and walks toward column 0.
void test_the_inbound_strip_runs_right_to_left(void) {
  strips::noteRx(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 9));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::EMPTY,   strips::slot(strips::INBOUND, 9));
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 8));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 7));
}

// §3 rule 6: the outbound strip runs left to right.
void test_the_outbound_strip_runs_left_to_right(void) {
  strips::noteTx(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::OUTBOUND, 0));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::EMPTY,   strips::slot(strips::OUTBOUND, 0));
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::OUTBOUND, 1));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::OUTBOUND, 2));
}

// The arrow enters at column 9 on the first tick, thus it sits on column 0
// after ten. The eleventh tick walks it off the end.
void test_an_arrow_leaves_the_far_end(void) {
  strips::noteRx(1);
  tickFast(10);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 0));

  tickFast(1);
  for (int x = 0; x < matrix::WIDTH; x++)
    TEST_ASSERT_EQUAL_INT(strips::EMPTY, strips::slot(strips::INBOUND, x));
}

// ── Layout ───────────────────────────────────────────────────────────────────

void test_each_strip_owns_three_rows(void) {
  strips::noteRx(1);
  tickFast(1);
  strips::render(BUS);

  const uint32_t lit = strips::scale(BUS, CANTICK_SHADE_A_PCT);
  for (int r = 0; r < CANTICK_STRIP_ROWS; r++)
    TEST_ASSERT_EQUAL_HEX32(lit, matrix::pixel(9, CANTICK_STRIP_IN_ROW0 + r));

  // The outbound strip saw no traffic, thus it stays dark.
  TEST_ASSERT_TRUE(stripRowsAreDark(CANTICK_STRIP_OUT_ROW0));
}

// ── Step rate ────────────────────────────────────────────────────────────────

// §4: below 50 % load the strip steps one pixel each 4 ticks.
void test_the_slow_step_rate_below_50_percent(void) {
  strips::noteRx(1);
  strips::tick(busload::LOW, 10);
  strips::tick(busload::LOW, 10);
  strips::tick(busload::LOW, 10);
  TEST_ASSERT_EQUAL_INT(strips::EMPTY, strips::slot(strips::INBOUND, 9));

  strips::tick(busload::LOW, 10);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 9));
}

// §4: at 50 % and above the strip steps one pixel each tick.
void test_the_fast_step_rate_at_50_percent_and_above(void) {
  strips::noteRx(1);
  strips::tick(busload::MID, 60);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 9));
}

// ── Shade pairs ──────────────────────────────────────────────────────────────

void test_arrows_alternate_two_shades(void) {
  strips::noteRx(4);
  tickFast(4);

  TEST_ASSERT_EQUAL_INT(strips::ARROW_B, strips::slot(strips::INBOUND, 9));
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 8));
  TEST_ASSERT_EQUAL_INT(strips::ARROW_B, strips::slot(strips::INBOUND, 7));
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 6));
}

// §4: below 90 % load the pair is 100 % and 40 %.
void test_the_shade_pair_below_90_percent(void) {
  strips::noteRx(10);
  for (int i = 0; i < 10; i++) strips::tick(busload::MID, 60);
  strips::render(BUS);

  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, 100), matrix::pixel(8, CANTICK_STRIP_IN_ROW0));
  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, 40),  matrix::pixel(9, CANTICK_STRIP_IN_ROW0));
}

// §4: from 90 % to 95 % the pair changes to 25 % and 15 %.
void test_the_shade_pair_at_the_high_band(void) {
  strips::noteRx(10);
  for (int i = 0; i < 10; i++) strips::tick(busload::HIGH, 92);
  strips::render(BUS);

  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, 25), matrix::pixel(8, CANTICK_STRIP_IN_ROW0));
  TEST_ASSERT_EQUAL_HEX32(strips::scale(BUS, 15), matrix::pixel(9, CANTICK_STRIP_IN_ROW0));
}

// The pair must differ between the two bands, or the test above proves nothing.
void test_the_two_shade_pairs_differ(void) {
  TEST_ASSERT_NOT_EQUAL(strips::scale(BUS, CANTICK_SHADE_A_PCT),
                        strips::scale(BUS, CANTICK_SHADE_A_HIGH_PCT));
}

// ── Pulse ────────────────────────────────────────────────────────────────────

// §3 rule 12: at 95 % and above the strip is a solid line.
void test_the_pulse_band_paints_a_solid_line(void) {
  strips::tick(busload::PULSE, 95);             // no traffic at all
  strips::render(BUS);

  const uint32_t lit = strips::scale(BUS, strips::pulseLevel());
  for (int x = 0; x < matrix::WIDTH; x++)
    TEST_ASSERT_EQUAL_HEX32(lit, matrix::pixel(x, CANTICK_STRIP_IN_ROW0));
}

// §4: the pulse swings from 100 % down to 25 %.
void test_the_pulse_swings_between_100_and_25(void) {
  uint32_t high = 0, low = 100;
  for (int i = 0; i < 40; i++) {
    strips::tick(busload::PULSE, 95);
    const uint32_t l = strips::pulseLevel();
    if (l > high) high = l;
    if (l < low)  low = l;
  }
  TEST_ASSERT_EQUAL_UINT32(CANTICK_PULSE_MAX_PCT, high);
  TEST_ASSERT_EQUAL_UINT32(CANTICK_PULSE_MIN_PCT, low);
}

void test_the_pulse_reports_a_change_on_every_tick(void) {
  TEST_ASSERT_TRUE(strips::tick(busload::PULSE, 95));
  TEST_ASSERT_TRUE(strips::tick(busload::PULSE, 95));
}

// ── Missing tooth ────────────────────────────────────────────────────────────

// §4: an increase of the drop counter blanks one slot on the inbound strip.
void test_a_drop_blanks_a_slot_on_the_inbound_strip(void) {
  strips::noteRx(3);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_A, strips::slot(strips::INBOUND, 9));

  strips::noteDrop(1);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::TOOTH, strips::slot(strips::INBOUND, 9));

  tickFast(1);
  TEST_ASSERT_EQUAL_INT(strips::ARROW_B, strips::slot(strips::INBOUND, 9));
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

// §3 rule 13: the tooth is black, in the place where the arrow goes.
void test_a_tooth_renders_black(void) {
  strips::noteRx(2);
  strips::noteDrop(1);
  tickFast(1);
  strips::render(BUS);

  for (int r = 0; r < CANTICK_STRIP_ROWS; r++)
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(9, CANTICK_STRIP_IN_ROW0 + r));
}

// A tooth stays black inside the solid pulsing line, or the loss is invisible.
void test_a_tooth_stays_black_in_the_pulse_band(void) {
  strips::noteRx(2);
  strips::noteDrop(1);
  strips::tick(busload::PULSE, 95);
  strips::render(BUS);

  TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(9, CANTICK_STRIP_IN_ROW0));
  TEST_ASSERT_NOT_EQUAL(0u, matrix::pixel(8, CANTICK_STRIP_IN_ROW0));
}

// ── Colour helper ────────────────────────────────────────────────────────────

void test_scale_works_one_channel_at_a_time(void) {
  TEST_ASSERT_EQUAL_HEX32(0x804020u, strips::scale(0x804020u, 100));
  TEST_ASSERT_EQUAL_HEX32(0x402010u, strips::scale(0x804020u, 50));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, strips::scale(0x804020u, 0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_strip_is_dark);
  RUN_TEST(test_a_quiet_strip_reports_no_change);
  RUN_TEST(test_the_inbound_strip_runs_right_to_left);
  RUN_TEST(test_the_outbound_strip_runs_left_to_right);
  RUN_TEST(test_an_arrow_leaves_the_far_end);
  RUN_TEST(test_each_strip_owns_three_rows);
  RUN_TEST(test_the_slow_step_rate_below_50_percent);
  RUN_TEST(test_the_fast_step_rate_at_50_percent_and_above);
  RUN_TEST(test_arrows_alternate_two_shades);
  RUN_TEST(test_the_shade_pair_below_90_percent);
  RUN_TEST(test_the_shade_pair_at_the_high_band);
  RUN_TEST(test_the_two_shade_pairs_differ);
  RUN_TEST(test_the_pulse_band_paints_a_solid_line);
  RUN_TEST(test_the_pulse_swings_between_100_and_25);
  RUN_TEST(test_the_pulse_reports_a_change_on_every_tick);
  RUN_TEST(test_a_drop_blanks_a_slot_on_the_inbound_strip);
  RUN_TEST(test_a_send_failure_blanks_a_slot_on_the_outbound_strip);
  RUN_TEST(test_a_tooth_renders_black);
  RUN_TEST(test_a_tooth_stays_black_in_the_pulse_band);
  RUN_TEST(test_scale_works_one_channel_at_a_time);
  return UNITY_END();
}
