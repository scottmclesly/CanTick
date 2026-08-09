#include "panel_hw.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "panel.h"
#include "cards.h"
#include "strips.h"
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

// The one call site that writes the panel. DISPLAY.md §3 rule 11: one show() per
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

#if CANTICK_PANEL_DEMO
// ── Bench simulation, not in a shipping build ────────────────────────────────
// It feeds the panel with synthetic counts so the idle layout can be checked
// with no bus attached. It calls panel::noteRx and panel::noteTx only. It never
// calls canlink::send(), thus it puts no frame on the bus and safety invariant
// 1 stands. env:s3_demo is the only build that sets the flag.
//
uint32_t g_demoTick = 0;
uint32_t g_demoStepTick = 0;
uint32_t g_demoStep = 0;

#if CANTICK_PANEL_VIDEO
// A loop for a short silent clip. It tells the whole story once every 28.5 s:
// the bus speed on a card, quiet traffic in that color, a build to saturation,
// and a fall back to quiet. It logs nothing.
constexpr uint32_t T_CARD  = 110;   //  5.5 s  the bus-speed card, strips paused
constexpr uint32_t T_LIGHT = 120;   //  6.0 s  light traffic
constexpr uint32_t T_BUILD = 160;   //  8.0 s  the build to saturation
constexpr uint32_t T_PEAK  =  60;   //  3.0 s  saturation held
constexpr uint32_t T_FALL  = 120;   //  6.0 s  the fall back to quiet
constexpr uint32_t T_LOOP  = T_CARD + T_LIGHT + T_BUILD + T_PEAK + T_FALL;

constexpr uint32_t PCT_QUIET = 15;
constexpr uint32_t PCT_FULL  = 97;
constexpr uint32_t PCT_END   = 5;

constexpr bool DEMO_TEETH = true;

uint32_t demoPercent() {
  const uint32_t p = g_demoTick++ % T_LOOP;

  if (p == 0) {
    const char *text = cards::busSpeedText(g_bitrate);
    if (text[0] != '\0')
      panel::raise(makeCard(cards::BUS_SPEED, text, g_busColor));
  }

  // The card holds the panel and both strips pause, thus feed nothing.
  if (p < T_CARD) return 0;
  uint32_t q = p - T_CARD;

  if (q < T_LIGHT) return PCT_QUIET;
  q -= T_LIGHT;

  if (q < T_BUILD) return PCT_QUIET + (PCT_FULL - PCT_QUIET) * q / T_BUILD;
  q -= T_BUILD;

  if (q < T_PEAK) return PCT_FULL;
  q -= T_PEAK;

  return PCT_FULL - (PCT_FULL - PCT_END) * q / T_FALL;
}
#else
// Six holds, 20 s each. 97 comes before 92 because the MID to HIGH rise edge is
// 95 %: a rising load steps straight to PULSE, and HIGH is only reachable on
// the way down.
const uint32_t DEMO_HOLD[6] = {10, 40, 80, 97, 92, 40};

constexpr bool DEMO_TEETH = false;

const char *bandName(busload::Band b) {
  switch (b) {
    case busload::BAND_LOW:   return "LOW";
    case busload::BAND_MID:   return "MID";
    case busload::BAND_HIGH:  return "HIGH";
    default:                  return "PULSE";
  }
}

uint32_t demoPercent() {
  const uint32_t hold  = 20u * CANTICK_MATRIX_TICK_HZ;
  const uint32_t cycle = 6u * hold;

  const uint32_t p = g_demoTick++ % cycle;
  const uint32_t h = p / hold;

  // Log one second into the hold, not at the changeover: the band needs the
  // 500 ms dwell to settle, thus a log at the edge names the previous band.
  static uint32_t lastH = 0xFFFFFFFFu;
  if (h != lastH && (p % hold) == CANTICK_MATRIX_TICK_HZ) {
    lastH = h;
    Serial.printf("[cantick]   hold %2u %%  band %s\n",
                  DEMO_HOLD[h], bandName(busload::band()));
  }

  return DEMO_HOLD[h];
}
#endif

void demoFeed(uint32_t pct, busload::Band band) {
  const uint32_t interval = (band == busload::BAND_LOW)
      ? CANTICK_STRIP_STEP_TICKS_SLOW
      : CANTICK_STRIP_STEP_TICKS_FAST;

  // Feed on the tick that steps, thus one mark lands in one opening slot.
  if (++g_demoStepTick < interval) return;
  g_demoStepTick = 0;
  g_demoStep++;

  // Real traffic is bursty, and the texture depends on it: two messages close
  // together are what read as a run. A regular feed would draw an even dotted
  // line. Emit at random with a mean of pct/100 of the columns lit.
  static uint32_t rng = 0xB5297A4Du;
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  if ((rng % (100u * CANTICK_MARK_COLUMNS)) < pct) {
    panel::noteRx(1);
    panel::noteTx(1);
  }

  // A missing tooth on each strip, on its own cadence. They read best against a
  // full strip, and the two periods never line up.
  if (DEMO_TEETH && pct > 0) {
    if (g_demoStep % 43 == 0) panel::noteDrop(1);
    if (g_demoStep % 67 == 0) panel::noteTxFail(1);
  }
}
#endif

void matrixTask(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
#if CANTICK_PANEL_DEMO
    // The band picks the step rate, thus the feed needs it before it runs.
    const uint32_t pct = demoPercent();
    const busload::Band band = busload::update(pct, millis());
    demoFeed(pct, band);
#else
    feed();
    const uint32_t pct = busload::percent(g_fps, g_bitrate);
    const busload::Band band = busload::update(pct, millis());
#endif
    panel::tick(band, g_busColor);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(CANTICK_MATRIX_TICK_MS));
  }
}

}  // namespace

namespace panelhw {

void begin(uint32_t bitrate) {
#if CANTICK_PANEL_VIDEO
  // The video loop shows one bus speed for the camera. This overrides the value
  // in memory only. NVS is untouched, and canlink already came up on the stored
  // speed, thus the bench and the shipping builds still read the board's own
  // value. No other build defines the flag.
  (void)bitrate;
  g_bitrate = CANTICK_VIDEO_BITRATE;
#else
  g_bitrate = bitrate;
#endif

#if CANTICK_PANEL_DEMO && !CANTICK_PANEL_VIDEO
  // The bench traffic is synthetic, thus the bus-speed color would mislead.
  g_busColor = CANTICK_RGB_TEST_DEFAULT;
#else
  // The video loop establishes the bus speed on a card, thus the traffic has to
  // carry the real color of that speed.
  g_busColor = cards::busSpeedColor(g_bitrate);
#endif

  g_pixels.begin();
  // DISPLAY.md §3 rule 10 sets the cap. A bench build may lower it to judge a
  // layout at a different level; no shipping environment defines the override.
#ifndef CANTICK_BENCH_BRIGHTNESS
#define CANTICK_BENCH_BRIGHTNESS CANTICK_MATRIX_BRIGHTNESS
#endif
  g_pixels.setBrightness(CANTICK_BENCH_BRIGHTNESS);
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
    panel::tick(busload::BAND_LOW, g_busColor);
    delay(CANTICK_MATRIX_TICK_MS);
  }
}

void raiseBusSpeedCard() {
#if CANTICK_PANEL_DEMO
  return;   // the bench shows the idle layout, thus it raises no card
#else
  const char *text = cards::busSpeedText(g_bitrate);
  if (text[0] == '\0') return;          // a speed the §5 table does not hold
  panel::raise(makeCard(cards::BUS_SPEED, text, g_busColor));
#endif
}

void startTask() {
  // Core 0, below net at priority 8. A WS2812B write is timing-critical, thus it
  // must not run on core 1 against the CAN drain.
  xTaskCreatePinnedToCore(matrixTask, "matrix", 4096, nullptr, 4, nullptr, 0);
}

}  // namespace panelhw
