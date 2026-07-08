#pragma once
#include <Arduino.h>
#include "can_link.h"

// WiFi station bring-up (primary → fallback AP), the SLCAN-over-TCP client, and
// the send-only UDP heartbeat. See PROTOCOL.md §1–§3.
namespace net {
  void begin();                 // load creds, start WiFi + tasks
  bool wifiConnected();
  int  rssi();

  // Called by the CAN RX sink: queue a received frame for the TCP stream.
  void enqueueRx(const CanFrame &f);

  // Applied by the provisioning layer after COMMIT.
  void applyCredentials();      // re-read NVS and (re)connect
}
