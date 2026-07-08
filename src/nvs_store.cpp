#include "config.h"
#include "nvs_store.h"
#include <Preferences.h>
#include <WiFi.h>

namespace {
  Preferences p;
  String g_name;

  String macName() {
    uint8_t m[6]; WiFi.macAddress(m);
    char b[16]; snprintf(b, sizeof(b), "cantick-%02x%02x%02x", m[3], m[4], m[5]);
    return String(b);
  }
}

namespace nvs {

void begin() {
  g_name = macName();
  p.begin(CANTICK_NVS_NS, false);
  if (p.getString("name", "").length() == 0) p.putString("name", g_name);
  p.end();
}

CtConfig load() {
  CtConfig c;
  p.begin(CANTICK_NVS_NS, true);   // read-only
  c.priSsid = p.getString("pri_ssid", "");
  c.priPsk  = p.getString("pri_psk",  "");
  c.fbSsid  = p.getString("fb_ssid",  CANTICK_FALLBACK_AP_SSID);
  c.fbPsk   = p.getString("fb_psk",   "");
  c.bitrate = p.getUInt("bitrate", CANTICK_DEFAULT_BITRATE);
  c.listen  = p.getUChar("listen", 0);
  c.name    = p.getString("name", g_name);
  p.end();
  return c;
}

void save(const CtConfig &c) {
  p.begin(CANTICK_NVS_NS, false);
  p.putString("pri_ssid", c.priSsid);
  p.putString("pri_psk",  c.priPsk);
  p.putString("fb_ssid",  c.fbSsid);
  p.putString("fb_psk",   c.fbPsk);
  p.putUInt("bitrate",    c.bitrate);
  p.putUChar("listen",    c.listen);
  if (c.name.length()) p.putString("name", c.name);
  p.end();
}

String deviceName() { return g_name.length() ? g_name : macName(); }

}  // namespace nvs
