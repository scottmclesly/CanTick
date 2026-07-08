#pragma once
#include <Arduino.h>
#include "config.h"

// Persisted configuration (NVS namespace "cantick"). See PROTOCOL.md §4.
struct CtConfig {
  String   priSsid, priPsk;
  String   fbSsid = CANTICK_FALLBACK_AP_SSID;
  String   fbPsk;
  uint32_t bitrate = CANTICK_DEFAULT_BITRATE;
  uint8_t  listen  = 0;
  String   name;                 // cantick-<mac6>, auto-filled if empty
};

namespace nvs {
  void      begin();             // open NVS, auto-fill device name
  CtConfig  load();
  void      save(const CtConfig &c);
  String    deviceName();        // cantick-<mac6>
}
