// Host tests for the card model and queue (Matrix-TODO.md Phase 4).
//
// ── The oracle comes from DISPLAY.md §6, §5 and §7 ───────────────────────────
//
// §6:  "A card runs to the end. It is not interrupted, and it is not merged
//       with another card.
//       A new event goes to a queue in the order of arrival. The bus-error card
//       is the one exception: it goes to the head of the queue.
//       The queue holds 4 cards. On overflow it drops the oldest. Two cards of
//       the same type in the queue collapse to one."
//
// §3 rule 2 and §7: a splash card scrolls one time, every other card scrolls
// two times.
//
// Two points that §6 leaves open. The tests below record the reading that this
// build uses. They do not change DISPLAY.md.
//
//   1. A collapse keeps the older entry's place in the order, and it takes the
//      newer text and color. Order then follows arrival, and content stays
//      current.
//   2. Overflow drops the oldest card to make room, and the new card lands
//      after that. A bus-error card that arrives at a full queue is thus never
//      the card that the overflow drops.

#include <unity.h>
#include "cards.h"

namespace {

cards::Card make(cards::Type t, const char *text, uint32_t color) {
  cards::Card c;
  c.type = t;
  c.text = text;
  c.color = color;
  c.scrolls = cards::scrollsFor(t);
  return c;
}

cards::Type headType() {
  cards::Card c;
  TEST_ASSERT_TRUE(cards::peek(c));
  return c.type;
}

}  // namespace

void setUp(void) { cards::reset(); }
void tearDown(void) {}

// ── Card model ───────────────────────────────────────────────────────────────

void test_a_splash_card_scrolls_one_time(void) {
  TEST_ASSERT_EQUAL_UINT8(1, cards::scrollsFor(cards::SPLASH_NAME));
  TEST_ASSERT_EQUAL_UINT8(1, cards::scrollsFor(cards::SPLASH_VERSION));
}

void test_every_other_card_scrolls_two_times(void) {
  TEST_ASSERT_EQUAL_UINT8(2, cards::scrollsFor(cards::BUS_SPEED));
  TEST_ASSERT_EQUAL_UINT8(2, cards::scrollsFor(cards::BUS_ERROR));
  TEST_ASSERT_EQUAL_UINT8(2, cards::scrollsFor(cards::WIFI_UP));
  TEST_ASSERT_EQUAL_UINT8(2, cards::scrollsFor(cards::WIFI_JOIN));
  TEST_ASSERT_EQUAL_UINT8(2, cards::scrollsFor(cards::WIFI_DOWN));
}

void test_a_card_carries_its_text_and_color(void) {
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));
  cards::Card c;
  TEST_ASSERT_TRUE(cards::peek(c));
  TEST_ASSERT_EQUAL_STRING("250 kbit/s", c.text);
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_BITRATE_250K, c.color);
  TEST_ASSERT_EQUAL_UINT8(2, c.scrolls);
}

// ── Order of arrival ─────────────────────────────────────────────────────────

void test_the_queue_holds_the_order_of_arrival(void) {
  cards::push(make(cards::WIFI_JOIN, "Wi-Fi", CANTICK_RGB_WIFI_JOIN));
  cards::push(make(cards::WIFI_UP, "Wi-Fi", CANTICK_RGB_WIFI_UP));
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));

  TEST_ASSERT_EQUAL_INT(3, cards::depth());

  cards::Card c;
  TEST_ASSERT_TRUE(cards::start(c));
  TEST_ASSERT_EQUAL_INT(cards::WIFI_JOIN, c.type);
  cards::finish();
  TEST_ASSERT_TRUE(cards::start(c));
  TEST_ASSERT_EQUAL_INT(cards::WIFI_UP, c.type);
  cards::finish();
  TEST_ASSERT_TRUE(cards::start(c));
  TEST_ASSERT_EQUAL_INT(cards::BUS_SPEED, c.type);
}

// ── Overflow ─────────────────────────────────────────────────────────────────

void test_overflow_drops_the_oldest(void) {
  cards::push(make(cards::SPLASH_NAME, "CANTick", 0x111111));
  cards::push(make(cards::SPLASH_VERSION, "V1.0", 0x222222));
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", 0x333333));
  cards::push(make(cards::WIFI_JOIN, "Wi-Fi", 0x444444));
  TEST_ASSERT_EQUAL_INT(CANTICK_CARD_QUEUE_DEPTH, cards::depth());

  cards::push(make(cards::WIFI_UP, "Wi-Fi", 0x555555));

  TEST_ASSERT_EQUAL_INT(CANTICK_CARD_QUEUE_DEPTH, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::SPLASH_VERSION, headType());   // SPLASH_NAME went
}

// ── Collapse ─────────────────────────────────────────────────────────────────

void test_two_cards_of_one_type_collapse(void) {
  cards::push(make(cards::WIFI_DOWN, "Wi-Fi", CANTICK_RGB_WIFI_DOWN));
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));
  cards::push(make(cards::WIFI_DOWN, "Wi-Fi", CANTICK_RGB_WIFI_DOWN));

  TEST_ASSERT_EQUAL_INT(2, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::WIFI_DOWN, headType());        // it kept its place
}

void test_a_collapse_takes_the_newer_text_and_color(void) {
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));
  cards::push(make(cards::BUS_SPEED, "500 kbit/s", CANTICK_RGB_BITRATE_500K));

  TEST_ASSERT_EQUAL_INT(1, cards::depth());
  cards::Card c;
  TEST_ASSERT_TRUE(cards::peek(c));
  TEST_ASSERT_EQUAL_STRING("500 kbit/s", c.text);
  TEST_ASSERT_EQUAL_HEX32(CANTICK_RGB_BITRATE_500K, c.color);
}

void test_a_collapse_into_a_full_queue_drops_nothing(void) {
  cards::push(make(cards::SPLASH_NAME, "CANTick", 0x111111));
  cards::push(make(cards::SPLASH_VERSION, "V1.0", 0x222222));
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", 0x333333));
  cards::push(make(cards::WIFI_JOIN, "Wi-Fi", 0x444444));

  cards::push(make(cards::BUS_SPEED, "500 kbit/s", 0x555555));

  TEST_ASSERT_EQUAL_INT(CANTICK_CARD_QUEUE_DEPTH, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::SPLASH_NAME, headType());      // nothing went
}

// ── Bus-error priority ───────────────────────────────────────────────────────

void test_a_bus_error_goes_to_the_head(void) {
  cards::push(make(cards::WIFI_JOIN, "Wi-Fi", CANTICK_RGB_WIFI_JOIN));
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));
  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));

  TEST_ASSERT_EQUAL_INT(3, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::BUS_ERROR, headType());
}

void test_two_bus_errors_collapse_at_the_head(void) {
  cards::push(make(cards::WIFI_JOIN, "Wi-Fi", CANTICK_RGB_WIFI_JOIN));
  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));
  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));

  TEST_ASSERT_EQUAL_INT(2, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::BUS_ERROR, headType());
}

// A bus error that arrives at a full queue survives. The overflow takes the
// oldest waiting card, not the card that just arrived.
void test_a_bus_error_survives_a_full_queue(void) {
  cards::push(make(cards::SPLASH_NAME, "CANTick", 0x111111));
  cards::push(make(cards::SPLASH_VERSION, "V1.0", 0x222222));
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", 0x333333));
  cards::push(make(cards::WIFI_JOIN, "Wi-Fi", 0x444444));

  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));

  TEST_ASSERT_EQUAL_INT(CANTICK_CARD_QUEUE_DEPTH, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::BUS_ERROR, headType());

  cards::Card c;
  cards::start(c);
  TEST_ASSERT_EQUAL_INT(cards::BUS_ERROR, c.type);
  cards::finish();
  TEST_ASSERT_EQUAL_INT(cards::SPLASH_VERSION, headType());   // SPLASH_NAME went
}

// ── A card runs to the end ───────────────────────────────────────────────────

void test_a_running_card_is_not_interrupted(void) {
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));

  cards::Card run;
  TEST_ASSERT_TRUE(cards::start(run));
  TEST_ASSERT_EQUAL_INT(cards::BUS_SPEED, run.type);
  TEST_ASSERT_TRUE(cards::running());

  // The worst case: the one card with priority arrives mid-scroll.
  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));

  cards::Card next;
  TEST_ASSERT_FALSE(cards::start(next));      // the running card holds the panel
  TEST_ASSERT_TRUE(cards::running());
  TEST_ASSERT_EQUAL_INT(1, cards::depth());
  TEST_ASSERT_EQUAL_INT(cards::BUS_ERROR, headType());
}

void test_the_next_card_starts_after_finish(void) {
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));
  cards::Card run;
  cards::start(run);
  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));

  cards::finish();
  TEST_ASSERT_FALSE(cards::running());
  TEST_ASSERT_TRUE(cards::start(run));
  TEST_ASSERT_EQUAL_INT(cards::BUS_ERROR, run.type);
  TEST_ASSERT_EQUAL_INT(0, cards::depth());
}

void test_start_on_an_empty_queue_gives_false(void) {
  cards::Card c;
  TEST_ASSERT_FALSE(cards::start(c));
  TEST_ASSERT_FALSE(cards::running());
}

void test_peek_on_an_empty_queue_gives_false(void) {
  cards::Card c;
  TEST_ASSERT_FALSE(cards::peek(c));
}

void test_reset_empties_the_queue_and_the_running_slot(void) {
  cards::push(make(cards::BUS_SPEED, "250 kbit/s", CANTICK_RGB_BITRATE_250K));
  cards::Card c;
  cards::start(c);
  cards::push(make(cards::BUS_ERROR, "X X X", CANTICK_RGB_BUS_ERROR));

  cards::reset();

  TEST_ASSERT_EQUAL_INT(0, cards::depth());
  TEST_ASSERT_FALSE(cards::running());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_splash_card_scrolls_one_time);
  RUN_TEST(test_every_other_card_scrolls_two_times);
  RUN_TEST(test_a_card_carries_its_text_and_color);
  RUN_TEST(test_the_queue_holds_the_order_of_arrival);
  RUN_TEST(test_overflow_drops_the_oldest);
  RUN_TEST(test_two_cards_of_one_type_collapse);
  RUN_TEST(test_a_collapse_takes_the_newer_text_and_color);
  RUN_TEST(test_a_collapse_into_a_full_queue_drops_nothing);
  RUN_TEST(test_a_bus_error_goes_to_the_head);
  RUN_TEST(test_two_bus_errors_collapse_at_the_head);
  RUN_TEST(test_a_bus_error_survives_a_full_queue);
  RUN_TEST(test_a_running_card_is_not_interrupted);
  RUN_TEST(test_the_next_card_starts_after_finish);
  RUN_TEST(test_start_on_an_empty_queue_gives_false);
  RUN_TEST(test_peek_on_an_empty_queue_gives_false);
  RUN_TEST(test_reset_empties_the_queue_and_the_running_slot);
  return UNITY_END();
}
