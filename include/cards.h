#pragma once
// ── Card model and queue ─────────────────────────────────────────────────────
// DISPLAY.md §5 gives the card vocabulary, §6 gives the queue rules and §7
// gives the boot splash.
//
// This unit owns card order, not card pixels. It draws nothing. It is pure and
// the host test build compiles it.

#include <stdint.h>
#include "config.h"

namespace cards {

// One type for each row of the DISPLAY.md §5 table, plus the two §7 splash
// cards. Two cards of one type collapse in the queue, thus a type is the
// identity that §6 acts on.
enum Type : uint8_t {
  SPLASH_NAME = 0,  // "CANTick"
  SPLASH_VERSION,   // the version, from a build macro, never a literal
  BUS_SPEED,        // the configured bus speed
  BUS_ERROR,        // "X X X"
  WIFI_UP,          // WiFi connected
  WIFI_JOIN,        // WiFi connects now
  WIFI_DOWN,        // WiFi disconnected
};

struct Card {
  Type        type;
  const char *text;     // the token that the panel scrolls
  uint32_t    color;    // 0xRRGGBB, from DISPLAY.md §5
  uint8_t     scrolls;  // how many times the card scrolls
};

// DISPLAY.md §3 rule 2 and §7: a splash card scrolls one time, and every other
// card scrolls two times.
uint8_t scrollsFor(Type type);

// The color and the text of the configured bus speed (DISPLAY.md §5). A bitrate
// that the table does not hold gives the default color and an empty text, thus
// the caller raises no card for it.
uint32_t    busSpeedColor(uint32_t bitrate);
const char *busSpeedText(uint32_t bitrate);

// Empty the queue and clear the running card.
void reset();

// Add a card, under the DISPLAY.md §6 rules:
//   A card of a type already in the queue collapses onto that entry. The entry
//   holds its place in the order, and it takes the newer text and color.
//   A full queue drops its oldest card to make room.
//   A bus-error card goes to the head. Every other card goes to the tail.
void push(const Card &c);

// How many cards wait. The running card is not one of them.
int depth();

// The card at the head, without removing it.
bool peek(Card &out);

// Move the head to the running slot. A card runs to the end (DISPLAY.md §6),
// thus this gives false while another card runs.
bool start(Card &out);

bool running();

// The running card reached its last scroll.
void finish();

}  // namespace cards
