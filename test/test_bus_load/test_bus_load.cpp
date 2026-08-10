// Host tests for the bus load estimate.
//
// ── The oracle comes from DISPLAY.md §3 rule 12 ──────────────────────────────
//
//   "Bus load is an estimate: load = frames_per_second x 111 / bitrate. The
//    constant 111 is the bit count of an 8-byte frame with stuffing. A
//    threshold has 5 % hysteresis on each side, and a state change needs a
//    500 ms dwell."
//
// The thresholds are 50 %, 90 % and 95 %, from §3 rules 9 to 12 and the §4
// table. Rule 15 puts a 5 % band on each side of every threshold except the
// HIGH to PULSE edge at 95 %, which has no band. That gives five edges:
//
//   rise LOW  -> MID    55 %        fall MID   -> LOW    45 %
//   rise MID  -> HIGH   95 %        fall HIGH  -> MID    85 %
//   rise HIGH -> PULSE  95 %        fall PULSE -> HIGH   below 95 %
//
// A load inside a dead band changes nothing. A load outside one starts a dwell,
// and the band moves only after the full 500 ms. The 95 % edge has no band,
// thus the dwell alone holds it steady.
//
// The 90 % rise edge and the pulse edge are both 95 %. A rising load therefore
// steps MID to PULSE, and HIGH appears on a falling load. Both bands paint a
// saturated bus, thus DISPLAY.md accepts that.
//
// Every expected percent below is worked by hand from rule 12. A bitrate of
// 111000 makes the arithmetic exact: load = fps / 10.

#include <unity.h>
#include "bus_load.h"

namespace {

// fps x 111 x 100 / 111000 = fps / 10, thus a round percent for a round fps.
constexpr uint32_t EXACT_BITRATE = 111000;

// Drive the state machine to a band and confirm it arrived.
void driveTo(busload::Band want, uint32_t pct, uint32_t t0) {
  busload::update(pct, t0);
  busload::update(pct, t0 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(want, busload::band());
}

// HIGH sits between two edges that are both 95 %, thus a rising load steps over
// it. Reach it on the way down: go to PULSE first, then fall below 95 %.
void driveToHigh(uint32_t t0) {
  driveTo(busload::BAND_PULSE, 95, t0);
  driveTo(busload::BAND_HIGH, 94, t0 + 2000);
}

}  // namespace

void setUp(void) { busload::reset(); }
void tearDown(void) {}

// ── Rule 15 arithmetic ───────────────────────────────────────────────────────

void test_percent_from_rule_15(void) {
  TEST_ASSERT_EQUAL_UINT32(50,  busload::percent(500,  EXACT_BITRATE));
  TEST_ASSERT_EQUAL_UINT32(90,  busload::percent(900,  EXACT_BITRATE));
  TEST_ASSERT_EQUAL_UINT32(100, busload::percent(1000, EXACT_BITRATE));
}

void test_percent_on_the_default_bitrate(void) {
  // 1000 x 111 x 100 / 250000 = 11100000 / 250000 = 44.4, truncated to 44.
  TEST_ASSERT_EQUAL_UINT32(44, busload::percent(1000, CANTICK_DEFAULT_BITRATE));
}

void test_percent_of_a_quiet_bus_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(0, busload::percent(0, CANTICK_DEFAULT_BITRATE));
}

void test_percent_with_no_bitrate_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(0, busload::percent(1000, 0));
}

// ── Hysteresis ───────────────────────────────────────────────────────────────

void test_starts_in_the_low_band(void) {
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
}

// 52 % is over the 50 % threshold but inside the 45 % to 55 % dead band.
void test_inside_the_dead_band_changes_nothing(void) {
  busload::update(52, 0);
  busload::update(52, 1000);
  busload::update(52, 60000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
}

void test_one_percent_short_of_the_rise_edge_changes_nothing(void) {
  busload::update(54, 0);
  busload::update(54, 60000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
}

// The accept criterion: a value crosses a threshold, then comes back inside the
// band before the dwell ends. The state must hold.
void test_cross_then_return_inside_the_band_holds_the_state(void) {
  busload::update(55, 0);           // outside the band, the dwell starts
  busload::update(52, 200);         // back inside, thus the change dies
  busload::update(52, 1000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());

  // A later run of the dwell still works from a clean start.
  busload::update(55, 2000);
  busload::update(55, 2000 + CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
  busload::update(55, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
}

// ── Dwell ────────────────────────────────────────────────────────────────────

// The accept criterion: a state change needs the full dwell.
void test_a_rise_needs_the_full_dwell(void) {
  busload::update(55, 0);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
  busload::update(55, CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
  busload::update(55, CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
}

void test_a_fall_needs_the_full_dwell(void) {
  driveTo(busload::BAND_MID, 60, 0);

  busload::update(45, 2000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
  busload::update(45, 2000 + CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
  busload::update(45, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
}

void test_a_falling_value_inside_the_band_holds_the_state(void) {
  driveTo(busload::BAND_MID, 60, 0);

  busload::update(48, 2000);        // inside 45 % to 55 %
  busload::update(48, 60000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
}

// ── The 90 % threshold ───────────────────────────────────────────────────────

// The pulse edge is 95 % with no band, thus 95 % goes straight to PULSE.
void test_a_load_at_95_reaches_the_pulse_band(void) {
  driveTo(busload::BAND_PULSE, 95, 0);
}

// ── The 95 % edge, which carries no hysteresis band ──────────────────────────

void test_a_full_bus_reaches_the_pulse_band(void) {
  driveTo(busload::BAND_PULSE, 100, 0);
}

// The 95 % edge has no band, thus 99 % reaches PULSE from HIGH.
void test_99_percent_reaches_the_pulse_band(void) {
  driveToHigh(0);

  driveTo(busload::BAND_PULSE, 99, 5000);
}

void test_96_percent_holds_the_pulse_band(void) {
  driveTo(busload::BAND_PULSE, 100, 0);

  busload::update(96, 2000);        // still at or above the 95 % edge
  busload::update(96, 60000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_PULSE, busload::band());
}

void test_pulse_falls_to_high_below_95(void) {
  driveTo(busload::BAND_PULSE, 100, 0);

  busload::update(94, 2000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_PULSE, busload::band());
  busload::update(94, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_HIGH, busload::band());
}

// No band on this edge does not mean no dwell. A load that moves across 95 %
// still waits the full 500 ms in each direction.
void test_the_95_edge_still_needs_the_full_dwell(void) {
  driveToHigh(0);

  busload::update(96, 5000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_HIGH, busload::band());
  busload::update(96, 5000 + CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::BAND_HIGH, busload::band());
  busload::update(96, 5000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_PULSE, busload::band());

  busload::update(94, 20000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_PULSE, busload::band());
  busload::update(94, 20000 + CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::BAND_PULSE, busload::band());
  busload::update(94, 20000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_HIGH, busload::band());
}

void test_a_full_bus_falls_two_bands_at_85(void) {
  driveTo(busload::BAND_PULSE, 100, 0);

  busload::update(85, 2000);
  busload::update(85, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
}

void test_87_percent_holds_the_high_band(void) {
  driveToHigh(0);

  busload::update(87, 5000);        // inside 85 % to 95 %
  busload::update(87, 60000);
  TEST_ASSERT_EQUAL_INT(busload::BAND_HIGH, busload::band());
}

void test_high_falls_to_mid_at_the_lower_edge(void) {
  driveToHigh(0);

  busload::update(85, 5000);
  busload::update(85, 5000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::band());
}

// ── Pending change ───────────────────────────────────────────────────────────

// A new target replaces the pending one, and the dwell starts again from there.
void test_a_new_target_starts_the_dwell_again(void) {
  busload::update(55, 0);           // pending MID, from 0 ms
  busload::update(95, 200);         // pending HIGH, from 200 ms
  busload::update(95, 600);         // 400 ms of dwell only
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
  busload::update(95, 700);         // 500 ms of dwell
  TEST_ASSERT_EQUAL_INT(busload::BAND_PULSE, busload::band());
}

void test_reset_returns_to_the_low_band(void) {
  driveTo(busload::BAND_PULSE, 95, 0);
  busload::reset();
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::band());
}

void test_update_gives_the_band_in_force(void) {
  TEST_ASSERT_EQUAL_INT(busload::BAND_LOW, busload::update(55, 0));
  TEST_ASSERT_EQUAL_INT(busload::BAND_MID, busload::update(55, CANTICK_LOAD_DWELL_MS));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_percent_from_rule_15);
  RUN_TEST(test_percent_on_the_default_bitrate);
  RUN_TEST(test_percent_of_a_quiet_bus_is_zero);
  RUN_TEST(test_percent_with_no_bitrate_is_zero);
  RUN_TEST(test_starts_in_the_low_band);
  RUN_TEST(test_inside_the_dead_band_changes_nothing);
  RUN_TEST(test_one_percent_short_of_the_rise_edge_changes_nothing);
  RUN_TEST(test_cross_then_return_inside_the_band_holds_the_state);
  RUN_TEST(test_a_rise_needs_the_full_dwell);
  RUN_TEST(test_a_fall_needs_the_full_dwell);
  RUN_TEST(test_a_falling_value_inside_the_band_holds_the_state);
  RUN_TEST(test_a_load_at_95_reaches_the_pulse_band);
  RUN_TEST(test_87_percent_holds_the_high_band);
  RUN_TEST(test_high_falls_to_mid_at_the_lower_edge);
  RUN_TEST(test_a_full_bus_reaches_the_pulse_band);
  RUN_TEST(test_99_percent_reaches_the_pulse_band);
  RUN_TEST(test_96_percent_holds_the_pulse_band);
  RUN_TEST(test_pulse_falls_to_high_below_95);
  RUN_TEST(test_the_95_edge_still_needs_the_full_dwell);
  RUN_TEST(test_a_full_bus_falls_two_bands_at_85);
  RUN_TEST(test_a_new_target_starts_the_dwell_again);
  RUN_TEST(test_reset_returns_to_the_low_band);
  RUN_TEST(test_update_gives_the_band_in_force);
  return UNITY_END();
}
