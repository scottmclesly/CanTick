#pragma once
#include <Arduino.h>
#include <stdint.h>

// A single classical-CAN frame (11- or 29-bit).
struct CanFrame {
  uint32_t id;        // arbitration ID (masked to 11 or 29 bits per `ext`)
  uint8_t  ext;       // 1 = 29-bit extended, 0 = 11-bit standard
  uint8_t  rtr;       // 1 = remote request frame
  uint8_t  dlc;       // 0..8
  uint8_t  data[8];
};

// Sink callback invoked for every frame received from the bus.
typedef void (*CanRxSink)(const CanFrame &f);

namespace canlink {
  // Bring up the MCP2515 (16 MHz xtal, CS=D7) at `bitrate`. Returns false on
  // init failure (bad wiring / wrong crystal). `listenOnly` selects MCP_LISTENONLY.
  bool begin(uint32_t bitrate, bool listenOnly);

  // Switch modes at runtime (SLCAN O vs L). Listen-only guarantees zero TX.
  void setListenOnly(bool on);
  bool isListenOnly();

  // Register the RX sink, then start the pinned polling drain task (core 1).
  void onFrame(CanRxSink sink);
  void startRxTask();

  // SAFETY: the ONLY transmit primitive. There is deliberately no bulk / blast /
  // replay / periodic-flood API. Returns false in listen-only mode or on error.
  bool send(const CanFrame &f);

  uint32_t rxCount();
  uint32_t txCount();
  uint32_t dropCount();   // MCP2515 RX FIFO overflow events (edge-counted).
                          // The outbound-queue-full count lives in net_transport;
                          // the heartbeat reports the sum as the contract `drop`.
}
