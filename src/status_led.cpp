#include "status_led.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
  volatile led::State g_state = led::BOOTING;
  volatile bool       g_fault = false;

  inline void write(bool on) {
#if CANTICK_STATUS_LED_ACTIVE_LOW
    digitalWrite(CANTICK_STATUS_LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(CANTICK_STATUS_LED_PIN, on ? HIGH : LOW);
#endif
  }

  // A blink pattern is an on/off time pair; each state gets a recognisable one.
  struct Pattern { uint16_t onMs, offMs; };
  Pattern patternFor(led::State s) {
    switch (s) {
      case led::BOOTING:   return {900, 100};   // near-solid, brief wink
      case led::WIFI:      return {500, 500};   // slow, even blink
      case led::NO_PI:     return {150, 850};   // short blip, long gap
      case led::STREAMING: return {60, 940};    // brief flash ~1 Hz: "alive"
      case led::LISTEN:    return {60, 240};    // faster flash: TX is disabled
      default:             return {500, 500};
    }
  }
  const Pattern FAULT = {120, 120};             // fast, urgent blink

  void ledTask(void *) {
    for (;;) {
      Pattern p = g_fault ? FAULT : patternFor(g_state);
      write(true);
      vTaskDelay(pdMS_TO_TICKS(p.onMs));
      write(false);
      vTaskDelay(pdMS_TO_TICKS(p.offMs));
    }
  }
}

namespace led {

void begin() {
  pinMode(CANTICK_STATUS_LED_PIN, OUTPUT);
  write(false);
  xTaskCreatePinnedToCore(ledTask, "led", 2048, nullptr, 1, nullptr, 0);
}

void set(State s)   { g_state = s; }
void fault(bool on) { g_fault = on; }

}  // namespace led
