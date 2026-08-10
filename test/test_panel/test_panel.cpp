// Host tests for the panel scheduler.
//
// ── The oracle comes from DISPLAY.md §2, §3 rule 11 and §6 ───────────────────
//
// §3.11 The animation tick is fixed at 20 Hz. Call show() one time per tick. Do
//       not call show() when the frame does not change.
// §2    A card takes all 6 rows. Both strips pause while a card scrolls. Both
//       strips resume when the card ends.
// §6    A card runs to the end. It is not interrupted.
//
// The strips must resume in the state they held, not reset to empty, or a card
// would blank the bus picture that it interrupted. The tests below take a slot
// snapshot before the card and compare it after.
//
// A fake driver stands in for the NeoPixel write. It counts calls and keeps the
// last frame, thus the tests can prove both the call gate and the wire order.

#include <unity.h>
#include "panel.h"
#include "matrix.h"
#include "strips.h"

namespace {

int      g_calls = 0;
uint32_t g_frame[CANTICK_MATRIX_PIXELS];
int      g_count = 0;

void fakeDriver(const uint32_t *wire, int count) {
  g_calls++;
  g_count = count;
  for (int i = 0; i < count && i < CANTICK_MATRIX_PIXELS; i++) g_frame[i] = wire[i];
}

constexpr uint32_t BUS = 0x804020;

// Every band except LOW steps the strips one pixel each tick.
void tickFast(int times) {
  for (int i = 0; i < times; i++) panel::tick(busload::BAND_MID, BUS);
}

cards::Card card(cards::Type t, const char *text, uint32_t color) {
  cards::Card c;
  c.type = t;
  c.text = text;
  c.color = color;
  c.scrolls = cards::scrollsFor(t);
  return c;
}

// One scroll runs the text from off the right edge to off the left edge. Each
// column costs CANTICK_CARD_SCROLL_STEP_TICKS ticks (DISPLAY.md §2).
int ticksPerScroll(const char *s) {
  return (CANTICK_MATRIX_COLS + matrix::textWidth(s) + 1)
         * CANTICK_CARD_SCROLL_STEP_TICKS;
}

void snapshotInbound(strips::Slot *out) {
  for (int x = 0; x < matrix::WIDTH; x++) out[x] = strips::slot(strips::INBOUND, x);
}

}  // namespace

void setUp(void) {
  g_calls = 0;
  g_count = 0;
  panel::begin(fakeDriver);
}
void tearDown(void) {}

// ── The driver call gate ─────────────────────────────────────────────────────

void test_the_first_frame_reaches_the_driver(void) {
  panel::tick(busload::BAND_MID, BUS);
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_INT(CANTICK_MATRIX_PIXELS, g_count);
}

// §3 rule 11: an unchanged frame raises no driver call.
void test_an_unchanged_frame_raises_no_driver_call(void) {
  panel::tick(busload::BAND_MID, BUS);      // the first frame always goes
  const int after = g_calls;

  panel::tick(busload::BAND_MID, BUS);
  panel::tick(busload::BAND_MID, BUS);
  panel::tick(busload::BAND_MID, BUS);

  TEST_ASSERT_EQUAL_INT(after, g_calls);   // a dark panel does not change
}

void test_a_changed_frame_raises_one_driver_call(void) {
  panel::tick(busload::BAND_MID, BUS);
  const int after = g_calls;

  panel::noteRx(1);
  panel::tick(busload::BAND_MID, BUS);      // an arrow enters, thus the frame moves

  TEST_ASSERT_EQUAL_INT(after + 1, g_calls);
}

void test_a_missing_driver_does_not_fault(void) {
  panel::begin(nullptr);
  panel::noteRx(1);
  panel::tick(busload::BAND_MID, BUS);      // it must not dereference a null
}

// The driver takes the frame in wire order. Column x row y sits at index
// x * 6 + (5 - y), from the DISPLAY.md §1 preset.
void test_the_driver_gets_the_frame_in_wire_order(void) {
  panel::noteRx(1);
  panel::tick(busload::BAND_MID, BUS);      // the mark enters at column 9

  // §4: the mark takes one of the inbound strip's 2 rows at random, thus the
  // row has to be read rather than assumed.
  const int row = strips::markRow(strips::INBOUND, 9);
  const int wire = 9 * CANTICK_MATRIX_ROWS + (CANTICK_MATRIX_ROWS - 1 - row);

  TEST_ASSERT_EQUAL_HEX32(matrix::pixel(9, row), g_frame[wire]);
  TEST_ASSERT_NOT_EQUAL(0u, g_frame[wire]);
}

// ── A card pauses the strips ─────────────────────────────────────────────────

void test_a_card_holds_the_panel(void) {
  panel::raise(card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K));
  panel::tick(busload::BAND_MID, BUS);

  TEST_ASSERT_TRUE(panel::cardRunning());
}

// §2: both strips pause while a card scrolls.
void test_a_card_pauses_both_strips(void) {
  panel::noteRx(4);
  panel::noteTx(4);
  tickFast(4);

  strips::Slot before[CANTICK_MATRIX_COLS];
  snapshotInbound(before);

  panel::raise(card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K));
  tickFast(5);
  TEST_ASSERT_TRUE(panel::cardRunning());

  for (int x = 0; x < matrix::WIDTH; x++)
    TEST_ASSERT_EQUAL_INT(before[x], strips::slot(strips::INBOUND, x));
}

// The strips resume in the state they held. A card never blanks the bus
// picture that it interrupted.
void test_the_strips_resume_in_the_state_they_held(void) {
  panel::noteRx(4);
  tickFast(4);

  strips::Slot before[CANTICK_MATRIX_COLS];
  snapshotInbound(before);

  panel::raise(card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K));
  const cards::Card c = card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K);
  tickFast(c.scrolls * ticksPerScroll("H"));
  TEST_ASSERT_FALSE(panel::cardRunning());

  // Still untouched the moment the card ends.
  for (int x = 0; x < matrix::WIDTH; x++)
    TEST_ASSERT_EQUAL_INT(before[x], strips::slot(strips::INBOUND, x));

  // The next tick walks the held slots on by one, from where they stopped.
  tickFast(1);
  for (int x = 0; x < matrix::WIDTH - 1; x++)
    TEST_ASSERT_EQUAL_INT(before[x + 1], strips::slot(strips::INBOUND, x));
}

// §6: a card runs to the end, thus a bus error waits its turn.
void test_a_card_is_not_interrupted(void) {
  panel::raise(card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K));
  tickFast(2);
  TEST_ASSERT_TRUE(panel::cardRunning());

  panel::raise(card(cards::BUS_ERROR, "X", CANTICK_RGB_BUS_ERROR));
  tickFast(2);
  TEST_ASSERT_TRUE(panel::cardRunning());
}

void test_a_card_scrolls_its_full_count(void) {
  const cards::Card c = card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K);
  TEST_ASSERT_EQUAL_UINT8(CANTICK_CARD_SCROLL_COUNT, c.scrolls);
  panel::raise(c);

  const int total = c.scrolls * ticksPerScroll("H");
  tickFast(total - 1);
  TEST_ASSERT_TRUE(panel::cardRunning());

  tickFast(1);
  TEST_ASSERT_FALSE(panel::cardRunning());
}

// §7: a splash card scrolls one time, thus it runs for half as long.
void test_a_splash_card_scrolls_one_time(void) {
  const cards::Card c = card(cards::SPLASH_NAME, "H", 0x00FF00);
  TEST_ASSERT_EQUAL_UINT8(CANTICK_SPLASH_SCROLL_COUNT, c.scrolls);
  panel::raise(c);

  tickFast(ticksPerScroll("H") - 1);
  TEST_ASSERT_TRUE(panel::cardRunning());

  tickFast(1);
  TEST_ASSERT_FALSE(panel::cardRunning());
}

// The queue holds the order, thus the second card runs after the first. The
// handover costs one tick: a card ends inside the tick that walks its text off
// the panel, and the next tick takes the new head. Both frames are blank there,
// because the text of each card is off the panel, thus nothing shows a gap.
void test_the_next_card_runs_after_the_first(void) {
  panel::raise(card(cards::SPLASH_NAME, "H", 0x00FF00));
  panel::raise(card(cards::SPLASH_VERSION, "I", 0x00FF00));

  tickFast(ticksPerScroll("H"));
  TEST_ASSERT_FALSE(panel::cardRunning());    // the first card ended

  tickFast(1);
  TEST_ASSERT_TRUE(panel::cardRunning());     // the second card took over

  tickFast(ticksPerScroll("I") - 1);
  TEST_ASSERT_FALSE(panel::cardRunning());
}

// A scrolling card moves one column each CANTICK_CARD_SCROLL_STEP_TICKS ticks,
// thus one step reaches the driver one time. The ticks between steps hold the
// same frame and must raise no call.
void test_a_scrolling_card_changes_the_frame_once_per_step(void) {
  panel::raise(card(cards::BUS_SPEED, "H", CANTICK_RGB_BITRATE_250K));
  panel::tick(busload::BAND_MID, BUS);
  const int after = g_calls;

  tickFast(CANTICK_CARD_SCROLL_STEP_TICKS);
  TEST_ASSERT_EQUAL_INT(after + 1, g_calls);
}

// ── Text width ───────────────────────────────────────────────────────────────

// ── Status pixel, DISPLAY.md §10 ─────────────────────────────────────────────

// The S3 keeps its onboard LED. No backend claims the pixel there, thus the
// panel draws none and (9, 5) still shows the strip under it.
// §2 leaves row 5 dark, thus the status pixel at (9, 5) shares its row with
// nothing. The S3 keeps its onboard LED and installs no backend, thus the pixel
// stays dark on that variant however busy the strips are.
void test_the_s3_default_draws_no_status_pixel(void) {
  TEST_ASSERT_FALSE(panel::statusPixelActive());

  panel::noteTx(10);
  panel::noteRx(10);
  tickFast(10);                                  // fill both strips

  TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(CANTICK_STATUS_PIXEL_X,
                                            CANTICK_STATUS_PIXEL_Y));
  for (int x = 0; x < matrix::WIDTH; x++)
    TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, CANTICK_MATRIX_ROWS - 1));
}

void test_the_color_map(void) {
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_STATUS_BOOTING, panel::statusColor(led::BOOTING, false));
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_WIFI_JOIN,      panel::statusColor(led::WIFI, false));
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_STATUS_NO_PI,   panel::statusColor(led::NO_PI, false));
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_WIFI_UP,        panel::statusColor(led::STREAMING, false));
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_STATUS_LISTEN,  panel::statusColor(led::LISTEN, false));
}

void test_the_fault_latch_wins_over_the_state(void) {
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_BUS_ERROR, panel::statusColor(led::STREAMING, true));
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_BUS_ERROR, panel::statusColor(led::BOOTING, true));
}

// §10: the pixel draws last, on top of a strip.
void test_the_status_pixel_draws_over_a_strip(void) {
  panel::noteTx(10);
  tickFast(10);                                  // the strip would light (9, 5)

  panel::statusPixelBackend(led::STREAMING, false, true);
  TEST_ASSERT_TRUE(panel::statusPixelActive());
  tickFast(1);

  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_WIFI_UP,
                          matrix::pixel(CANTICK_STATUS_PIXEL_X, CANTICK_STATUS_PIXEL_Y));
}

void test_the_status_pixel_is_dark_off_the_blink_phase(void) {
  panel::noteTx(10);
  tickFast(10);

  panel::statusPixelBackend(led::STREAMING, false, false);
  tickFast(1);

  TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(CANTICK_STATUS_PIXEL_X,
                                            CANTICK_STATUS_PIXEL_Y));
}

// §10: the pixel draws last, on top of a card. It must survive every tick of a
// full card, not only the first.
void test_the_status_pixel_survives_a_full_card(void) {
  panel::statusPixelBackend(led::NO_PI, false, true);
  const cards::Card c = card(cards::BUS_SPEED, "HIH", CANTICK_RGB_BITRATE_250K);
  panel::raise(c);

  const int total = c.scrolls * ticksPerScroll("HIH");
  for (int i = 0; i < total; i++) {
    panel::tick(busload::BAND_MID, BUS);
    TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_STATUS_NO_PI,
                            matrix::pixel(CANTICK_STATUS_PIXEL_X,
                                          CANTICK_STATUS_PIXEL_Y));
  }
  TEST_ASSERT_FALSE(panel::cardRunning());
}

// Card text holds rows 0 to 4, thus a glyph can never reach the pixel.
void test_card_text_never_reaches_row_5(void) {
  panel::raise(card(cards::BUS_SPEED, "HIH", CANTICK_RGB_BITRATE_250K));

  for (int i = 0; i < 2 * ticksPerScroll("HIH"); i++) {
    panel::tick(busload::BAND_MID, BUS);
    for (int x = 0; x < matrix::WIDTH; x++)
      TEST_ASSERT_EQUAL_HEX32(0u, matrix::pixel(x, CANTICK_STATUS_PIXEL_Y));
  }
}

// The blink must reach the panel, or the status output would freeze.
void test_a_blink_phase_change_reaches_the_driver(void) {
  panel::statusPixelBackend(led::STREAMING, false, true);
  tickFast(1);
  const int after = g_calls;

  panel::statusPixelBackend(led::STREAMING, false, false);
  tickFast(1);
  TEST_ASSERT_EQUAL_INT(after + 1, g_calls);
}

void test_text_width_counts_the_gaps_between_glyphs(void) {
  TEST_ASSERT_EQUAL_INT(0,  matrix::textWidth(""));
  TEST_ASSERT_EQUAL_INT(3,  matrix::textWidth("H"));
  TEST_ASSERT_EQUAL_INT(7,  matrix::textWidth("HI"));
  TEST_ASSERT_EQUAL_INT(11, matrix::textWidth("HIH"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_first_frame_reaches_the_driver);
  RUN_TEST(test_an_unchanged_frame_raises_no_driver_call);
  RUN_TEST(test_a_changed_frame_raises_one_driver_call);
  RUN_TEST(test_a_missing_driver_does_not_fault);
  RUN_TEST(test_the_driver_gets_the_frame_in_wire_order);
  RUN_TEST(test_a_card_holds_the_panel);
  RUN_TEST(test_a_card_pauses_both_strips);
  RUN_TEST(test_the_strips_resume_in_the_state_they_held);
  RUN_TEST(test_a_card_is_not_interrupted);
  RUN_TEST(test_a_card_scrolls_its_full_count);
  RUN_TEST(test_a_splash_card_scrolls_one_time);
  RUN_TEST(test_the_next_card_runs_after_the_first);
  RUN_TEST(test_a_scrolling_card_changes_the_frame_once_per_step);
  RUN_TEST(test_the_s3_default_draws_no_status_pixel);
  RUN_TEST(test_the_color_map);
  RUN_TEST(test_the_fault_latch_wins_over_the_state);
  RUN_TEST(test_the_status_pixel_draws_over_a_strip);
  RUN_TEST(test_the_status_pixel_is_dark_off_the_blink_phase);
  RUN_TEST(test_the_status_pixel_survives_a_full_card);
  RUN_TEST(test_card_text_never_reaches_row_5);
  RUN_TEST(test_a_blink_phase_change_reaches_the_driver);
  RUN_TEST(test_text_width_counts_the_gaps_between_glyphs);
  return UNITY_END();
}
