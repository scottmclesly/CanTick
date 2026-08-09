#include "cards.h"

namespace {

cards::Card g_queue[CANTICK_CARD_QUEUE_DEPTH];
int         g_count = 0;

cards::Card g_running;
bool        g_isRunning = false;

// Remove the head and close the gap. The queue is 4 deep, thus a shift costs
// less than the state a ring buffer needs.
void dropOldest() {
  if (g_count == 0) return;
  for (int i = 1; i < g_count; i++) g_queue[i - 1] = g_queue[i];
  g_count--;
}

int findType(cards::Type t) {
  for (int i = 0; i < g_count; i++)
    if (g_queue[i].type == t) return i;
  return -1;
}

}  // namespace

namespace cards {

uint8_t scrollsFor(Type type) {
  if (type == SPLASH_NAME || type == SPLASH_VERSION)
    return CANTICK_SPLASH_SCROLL_COUNT;
  return CANTICK_CARD_SCROLL_COUNT;
}

uint32_t busSpeedColor(uint32_t bitrate) {
  switch (bitrate) {
    case 1000000: return CANTICK_RGB_BITRATE_1M;
    case 500000:  return CANTICK_RGB_BITRATE_500K;
    case 250000:  return CANTICK_RGB_BITRATE_250K;
    case 125000:  return CANTICK_RGB_BITRATE_125K;
    case 100000:  return CANTICK_RGB_BITRATE_100K;
    case 50000:   return CANTICK_RGB_BITRATE_50K;
  }
  return CANTICK_RGB_BITRATE_250K;      // the color of the default bitrate
}

const char *busSpeedText(uint32_t bitrate) {
  switch (bitrate) {
    case 1000000: return "1 Mbit/s";
    case 500000:  return "500 kbit/s";
    case 250000:  return "250 kbit/s";
    case 125000:  return "125 kbit/s";
    case 100000:  return "100 kbit/s";
    case 50000:   return "50 kbit/s";
  }
  return "";                            // no card for a speed the table lacks
}

void reset() {
  g_count = 0;
  g_isRunning = false;
}

void push(const Card &c) {
  // Collapse first. A duplicate type never grows the queue, thus it never
  // forces a drop.
  const int at = findType(c.type);
  if (at >= 0) {
    g_queue[at] = c;                    // the newer text and color win
    return;
  }

  // Make room before the insert. The drop then takes the oldest waiting card,
  // and it never takes the card that this call adds.
  if (g_count == CANTICK_CARD_QUEUE_DEPTH) dropOldest();

  if (c.type == BUS_ERROR) {
    for (int i = g_count; i > 0; i--) g_queue[i] = g_queue[i - 1];
    g_queue[0] = c;
  } else {
    g_queue[g_count] = c;
  }
  g_count++;
}

int depth() { return g_count; }

bool peek(Card &out) {
  if (g_count == 0) return false;
  out = g_queue[0];
  return true;
}

bool start(Card &out) {
  if (g_isRunning) return false;        // a card runs to the end, DISPLAY.md §6
  if (g_count == 0) return false;
  g_running = g_queue[0];
  dropOldest();
  g_isRunning = true;
  out = g_running;
  return true;
}

bool running() { return g_isRunning; }

void finish() { g_isRunning = false; }

}  // namespace cards
