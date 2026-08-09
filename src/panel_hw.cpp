#include "panel_hw.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "panel.h"
#include "cards.h"
#include "bus_load.h"
#include "can_link.h"
#include "net_transport.h"

namespace {

// DISPLAY.md §1: D0 is a constexpr, not a macro. Take the constexpr here, at the
// point of use. Never write #ifdef on a pad name.
constexpr int MATRIX_PIN = CANTICK_MATRIX_PIN;

// DISPLAY.md §1 locks the color order as GRB.
Adafruit_NeoPixel g_pixels(CANTICK_MATRIX_PIXELS, MATRIX_PIN, NEO_GRB + NEO_KHZ800);

// The splash text. §7: the version comes from the build macro, never a literal,
// thus the splash and the build can never disagree.
const char *SPLASH_NAME_TEXT    = "CANTick";
const char *SPLASH_VERSION_TEXT = "V" CANTICK_FW_VERSION;

uint32_t g_bitrate  = CANTICK_DEFAULT_BITRATE;
uint32_t g_busColor = CANTICK_RGB_BITRATE_250K;

// Counter marks. The strips take deltas, thus the hot RX path on core 1 gains
// no work at all.
uint32_t g_lastRx = 0, g_lastTx = 0, g_lastDrop = 0, g_lastTxFail = 0;

// One second of ticks gives the frame rate that the load estimate needs.
uint32_t g_windowFrames = 0;
uint32_t g_windowTicks = 0;
uint32_t g_fps = 0;

// The one call site that writes the panel. DISPLAY.md §3 rule 14: one show() per
// tick, and only when the frame changes. panel decides when to call this.
void driver(const uint32_t *wire, int count) {
  for (int i = 0; i < count; i++) {
    const uint32_t c = wire[i];
    g_pixels.setPixelColor(i, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
  }
  g_pixels.show();
}

// The heartbeat `drop` field is the sum of two counters (CLAUDE.md "Drop
// accounting"). The inbound strip blanks a slot on an increase of either.
uint32_t dropTotal() { return canlink::dropCount() + net::dropCount(); }

cards::Card makeCard(cards::Type t, const char *text, uint32_t color) {
  cards::Card c;
  c.type = t;
  c.text = text;
  c.color = color;
  c.scrolls = cards::scrollsFor(t);
  return c;
}

void feed() {
  const uint32_t rx = canlink::rxCount();
  const uint32_t tx = canlink::txCount();
  const uint32_t dr = dropTotal();
  const uint32_t tf = canlink::txFailCount();

  const uint32_t dRx     = rx - g_lastRx;    // unsigned, thus a wrap is safe
  const uint32_t dTx     = tx - g_lastTx;
  const uint32_t dDrop   = dr - g_lastDrop;
  const uint32_t dTxFail = tf - g_lastTxFail;

  g_lastRx = rx;
  g_lastTx = tx;
  g_lastDrop = dr;
  g_lastTxFail = tf;

  if (dRx)     panel::noteRx(dRx);
  if (dTx)     panel::noteTx(dTx);
  if (dDrop)   panel::noteDrop(dDrop);
  if (dTxFail) panel::noteTxFail(dTxFail);

  g_windowFrames += dRx + dTx;
  if (++g_windowTicks >= CANTICK_MATRIX_TICK_HZ) {
    g_fps = g_windowFrames;
    g_windowFrames = 0;
    g_windowTicks = 0;
  }
}

void matrixTask(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    feed();
    const uint32_t pct = busload::percent(g_fps, g_bitrate);
    const busload::Band band = busload::update(pct, millis());
    panel::tick(band, pct, g_busColor);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(CANTICK_MATRIX_TICK_MS));
  }
}

}  // namespace

namespace panelhw {

void begin(uint32_t bitrate) {
  g_bitrate = bitrate;
  g_busColor = cards::busSpeedColor(bitrate);

  g_pixels.begin();
  g_pixels.setBrightness(CANTICK_MATRIX_BRIGHTNESS);   // DISPLAY.md §3 rule 16
  g_pixels.clear();
  g_pixels.show();

  panel::begin(driver);

  g_lastRx = canlink::rxCount();
  g_lastTx = canlink::txCount();
  g_lastDrop = dropTotal();
  g_lastTxFail = canlink::txFailCount();
}

void runSplash() {
  panel::raise(makeCard(cards::SPLASH_NAME,    SPLASH_NAME_TEXT,    CANTICK_RGB_SPLASH));
  panel::raise(makeCard(cards::SPLASH_VERSION, SPLASH_VERSION_TEXT, CANTICK_RGB_SPLASH));

  // The tick runs here, on setup()'s task. The caller starts WiFi after this
  // returns, thus no animation runs through a connect attempt (DISPLAY.md §7).
  while (!panel::idle()) {
    panel::tick(busload::BAND_LOW, 0, g_busColor);
    delay(CANTICK_MATRIX_TICK_MS);
  }
}

void raiseBusSpeedCard() {
  const char *text = cards::busSpeedText(g_bitrate);
  if (text[0] == '\0') return;          // a speed the §5 table does not hold
  panel::raise(makeCard(cards::BUS_SPEED, text, g_busColor));
}

void startTask() {
  // Core 0, below net at priority 8. A WS2812B write is timing-critical, thus it
  // must not run on core 1 against the CAN drain.
  xTaskCreatePinnedToCore(matrixTask, "matrix", 4096, nullptr, 4, nullptr, 0);
}

}  // namespace panelhw
