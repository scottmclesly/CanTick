#include "panel.h"
#include "matrix.h"
#include "strips.h"

namespace {

panel::Driver g_driver = nullptr;

// The status pixel. The status_led task writes these, and the matrix task reads
// them. Each is a single aligned word, thus volatile is enough and this file
// stays free of FreeRTOS.
volatile uint32_t g_statusColor = 0;
volatile bool     g_statusLit = false;
volatile bool     g_statusActive = false;

uint32_t g_last[CANTICK_MATRIX_PIXELS];
bool     g_haveLast = false;

cards::Card g_card;
int32_t     g_scrollX = 0;
uint8_t     g_scrollsLeft = 0;
bool        g_cardRunning = false;

void clearState() {
  cards::reset();
  strips::reset();
  matrix::clear();
  g_haveLast = false;
  g_cardRunning = false;
  g_scrollX = 0;
  g_scrollsLeft = 0;
  g_statusColor = 0;
  g_statusLit = false;
  g_statusActive = false;
}

// Take the head of the queue, if no card runs. §6: a card runs to the end.
void startCardIfIdle() {
  cards::Card c;
  if (!cards::start(c)) return;
  g_card = c;
  g_scrollsLeft = c.scrolls;
  g_scrollX = CANTICK_MATRIX_COLS;      // the text waits off the right edge
  g_cardRunning = true;
}

void advanceCard() {
  g_scrollX -= CANTICK_CARD_SCROLL_STEP_COLS;
  if (g_scrollX >= -matrix::textWidth(g_card.text)) return;

  if (g_scrollsLeft > 0) g_scrollsLeft--;
  if (g_scrollsLeft == 0) {
    cards::finish();
    g_cardRunning = false;
  } else {
    g_scrollX = CANTICK_MATRIX_COLS;
  }
}

// Send the frame, and only when it differs from the frame already on the panel.
void push() {
  uint32_t wire[CANTICK_MATRIX_PIXELS];
  matrix::toWire(wire);

  if (g_haveLast) {
    bool same = true;
    for (int i = 0; i < CANTICK_MATRIX_PIXELS; i++) {
      if (wire[i] != g_last[i]) { same = false; break; }
    }
    if (same) return;                   // DISPLAY.md §3 rule 14
  }

  for (int i = 0; i < CANTICK_MATRIX_PIXELS; i++) g_last[i] = wire[i];
  g_haveLast = true;

  if (g_driver) g_driver(wire, CANTICK_MATRIX_PIXELS);
}

}  // namespace

namespace panel {

void begin(Driver driver) {
  g_driver = driver;
  clearState();
}

void reset() { clearState(); }

void raise(const cards::Card &c) { cards::push(c); }

void noteRx(uint32_t frames)    { strips::noteRx(frames); }
void noteTx(uint32_t frames)    { strips::noteTx(frames); }
void noteDrop(uint32_t count)   { strips::noteDrop(count); }
void noteTxFail(uint32_t count) { strips::noteTxFail(count); }

void tick(busload::Band band, uint32_t loadPercent, uint32_t busColor) {
  if (!g_cardRunning) startCardIfIdle();

  matrix::clear();

  if (g_cardRunning) {
    // §2 and §3 rule 4: the card owns all 6 rows. strips::tick() is not called,
    // thus both strips hold their slots and resume where they stopped.
    matrix::drawText(g_card.text, g_scrollX, CANTICK_CARD_TEXT_ROW, g_card.color);
    advanceCard();
  } else {
    strips::tick(band, loadPercent);
    strips::render(busColor);
  }

  // §10: the status pixel draws last, on top of a card or a strip, thus nothing
  // hides it.
  if (g_statusActive) {
    matrix::setPixel(CANTICK_STATUS_PIXEL_X, CANTICK_STATUS_PIXEL_Y,
                     g_statusLit ? g_statusColor : 0);
  }

  push();
}

bool cardRunning() { return g_cardRunning; }

uint32_t statusColor(led::State s, bool fault) {
  if (fault) return CANTICK_RGB_BUS_ERROR;      // the latch wins over the state

  switch (s) {
    case led::BOOTING:   return CANTICK_RGB_STATUS_BOOTING;
    case led::WIFI:      return CANTICK_RGB_WIFI_JOIN;
    case led::NO_PI:     return CANTICK_RGB_STATUS_NO_PI;
    case led::STREAMING: return CANTICK_RGB_WIFI_UP;
    case led::LISTEN:    return CANTICK_RGB_STATUS_LISTEN;
  }
  return 0;
}

void statusPixelBackend(led::State s, bool fault, bool on) {
  g_statusColor = statusColor(s, fault);
  g_statusLit = on;
  g_statusActive = true;
}

bool statusPixelActive() { return g_statusActive; }

}  // namespace panel
