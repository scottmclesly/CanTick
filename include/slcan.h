#pragma once
#include <Arduino.h>
#include "can_link.h"

// SLCAN / LAWICEL ASCII codec — the subset slcand drives (see PROTOCOL.md §1).
namespace slcan {

  // Result of feeding one complete '\r'-terminated command line.
  enum Action { NONE, OK, ERR, OPEN, OPEN_LISTEN, CLOSE, SET_BITRATE, TX_FRAME, INFO };

  struct ParseResult {
    Action    action = NONE;
    uint32_t  bitrate = 0;     // valid when action == SET_BITRATE
    CanFrame  frame;           // valid when action == TX_FRAME
    String    reply;           // bytes to write back (INFO/OK/ERR already filled)
  };

  // Parse one line (without the trailing '\r'). Pure function — no I/O.
  ParseResult parseLine(const String &line);

  // Encode a received bus frame as an SLCAN line INCLUDING trailing '\r'.
  String encodeFrame(const CanFrame &f);

  // Map a LAWICEL 'S' code (0..8) to a numeric bitrate; 0 if unknown.
  uint32_t bitrateForCode(int code);
}
