// Host tests for the bus load estimate (Matrix-TODO.md Phase 3).
//
// ── The oracle comes from DISPLAY.md §3 rule 15 ──────────────────────────────
//
//   "Bus load is an estimate: load = frames_per_second x 111 / bitrate. The
//    constant 111 is the bit count of an 8-byte frame with stuffing. A
//    threshold has 5 % hysteresis on each side, and a state change needs a
//    500 ms dwell."
//
// The thresholds are 50 %, 90 % and 95 %, from §3 rules 9 to 12 and the §4
// table. A 5 % band on each side gives six edges:
//
//   rise LOW  -> MID     55 %       fall MID   -> LOW    45 %
//   rise MID  -> HIGH    95 %       fall HIGH  -> MID    85 %
//   rise HIGH -> PULSE  100 %       fall PULSE -> HIGH   90 %
//
// A load inside a dead band changes nothing. A load outside one starts a dwell,
// and the band moves only after the full 500 ms.
//
// The 90 % and 95 % thresholds sit 5 % apart, thus their dead zones overlap.
// The tests below record what the two DISPLAY.md rules give together. They do
// not correct either rule.
//
// Every expected percent below is worked by hand from rule 15. A bitrate of
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
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
}

// 52 % is over the 50 % threshold but inside the 45 % to 55 % dead band.
void test_inside_the_dead_band_changes_nothing(void) {
  busload::update(52, 0);
  busload::update(52, 1000);
  busload::update(52, 60000);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
}

void test_one_percent_short_of_the_rise_edge_changes_nothing(void) {
  busload::update(54, 0);
  busload::update(54, 60000);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
}

// The accept criterion: a value crosses a threshold, then comes back inside the
// band before the dwell ends. The state must hold.
void test_cross_then_return_inside_the_band_holds_the_state(void) {
  busload::update(55, 0);           // outside the band, the dwell starts
  busload::update(52, 200);         // back inside, thus the change dies
  busload::update(52, 1000);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());

  // A later run of the dwell still works from a clean start.
  busload::update(55, 2000);
  busload::update(55, 2000 + CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
  busload::update(55, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
}

// ── Dwell ────────────────────────────────────────────────────────────────────

// The accept criterion: a state change needs the full dwell.
void test_a_rise_needs_the_full_dwell(void) {
  busload::update(55, 0);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
  busload::update(55, CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
  busload::update(55, CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
}

void test_a_fall_needs_the_full_dwell(void) {
  driveTo(busload::MID, 60, 0);

  busload::update(45, 2000);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
  busload::update(45, 2000 + CANTICK_LOAD_DWELL_MS - 1);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
  busload::update(45, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
}

void test_a_falling_value_inside_the_band_holds_the_state(void) {
  driveTo(busload::MID, 60, 0);

  busload::update(48, 2000);        // inside 45 % to 55 %
  busload::update(48, 60000);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
}

// ── The 90 % threshold ───────────────────────────────────────────────────────

void test_a_load_at_95_reaches_high_in_one_dwell(void) {
  driveTo(busload::HIGH, 95, 0);
}

// ── The 95 % threshold ───────────────────────────────────────────────────────

void test_a_full_bus_reaches_the_pulse_band(void) {
  driveTo(busload::PULSE, 100, 0);
}

// The rise edge of the 95 % threshold is 100 %, thus 99 % holds the HIGH band.
void test_99_percent_does_not_reach_the_pulse_band(void) {
  driveTo(busload::HIGH, 95, 0);

  busload::update(99, 2000);
  busload::update(99, 60000);
  TEST_ASSERT_EQUAL_INT(busload::HIGH, busload::band());
}

void test_92_percent_holds_the_pulse_band(void) {
  driveTo(busload::PULSE, 100, 0);

  busload::update(92, 2000);        // inside 90 % to 100 %
  busload::update(92, 60000);
  TEST_ASSERT_EQUAL_INT(busload::PULSE, busload::band());
}

void test_pulse_falls_to_high_at_the_lower_edge(void) {
  driveTo(busload::PULSE, 100, 0);

  busload::update(90, 2000);
  TEST_ASSERT_EQUAL_INT(busload::PULSE, busload::band());
  busload::update(90, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::HIGH, busload::band());
}

void test_a_full_bus_falls_two_bands_at_85(void) {
  driveTo(busload::PULSE, 100, 0);

  busload::update(85, 2000);
  busload::update(85, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
}

void test_87_percent_holds_the_high_band(void) {
  driveTo(busload::HIGH, 95, 0);

  busload::update(87, 2000);        // inside 85 % to 95 %
  busload::update(87, 60000);
  TEST_ASSERT_EQUAL_INT(busload::HIGH, busload::band());
}

void test_high_falls_to_mid_at_the_lower_edge(void) {
  driveTo(busload::HIGH, 95, 0);

  busload::update(85, 2000);
  busload::update(85, 2000 + CANTICK_LOAD_DWELL_MS);
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::band());
}

// ── Pending change ───────────────────────────────────────────────────────────

// A new target replaces the pending one, and the dwell starts again from there.
void test_a_new_target_starts_the_dwell_again(void) {
  busload::update(55, 0);           // pending MID, from 0 ms
  busload::update(95, 200);         // pending HIGH, from 200 ms
  busload::update(95, 600);         // 400 ms of dwell only
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
  busload::update(95, 700);         // 500 ms of dwell
  TEST_ASSERT_EQUAL_INT(busload::HIGH, busload::band());
}

void test_reset_returns_to_the_low_band(void) {
  driveTo(busload::HIGH, 95, 0);
  busload::reset();
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::band());
}

void test_update_gives_the_band_in_force(void) {
  TEST_ASSERT_EQUAL_INT(busload::LOW, busload::update(55, 0));
  TEST_ASSERT_EQUAL_INT(busload::MID, busload::update(55, CANTICK_LOAD_DWELL_MS));
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
  RUN_TEST(test_a_load_at_95_reaches_high_in_one_dwell);
  RUN_TEST(test_87_percent_holds_the_high_band);
  RUN_TEST(test_high_falls_to_mid_at_the_lower_edge);
  RUN_TEST(test_a_full_bus_reaches_the_pulse_band);
  RUN_TEST(test_99_percent_does_not_reach_the_pulse_band);
  RUN_TEST(test_92_percent_holds_the_pulse_band);
  RUN_TEST(test_pulse_falls_to_high_at_the_lower_edge);
  RUN_TEST(test_a_full_bus_falls_two_bands_at_85);
  RUN_TEST(test_a_new_target_starts_the_dwell_again);
  RUN_TEST(test_reset_returns_to_the_low_band);
  RUN_TEST(test_update_gives_the_band_in_force);
  return UNITY_END();
}
