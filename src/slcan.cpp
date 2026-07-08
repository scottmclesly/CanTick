#include "slcan.h"
#include "config.h"

namespace {
  const char CR = '\r';
  const char BELL = '\a';

  int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  // Parse `n` hex chars from s[pos..] into out. Returns false on bad hex.
  bool hexN(const String &s, int pos, int n, uint32_t &out) {
    if (pos + n > (int)s.length()) return false;
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
      int h = hexNibble(s[pos + i]);
      if (h < 0) return false;
      v = (v << 4) | (uint32_t)h;
    }
    out = v;
    return true;
  }
  void appendHex(String &s, uint32_t v, int digits) {
    static const char *H = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) s += H[(v >> (4 * i)) & 0xF];
  }
}

namespace slcan {

uint32_t bitrateForCode(int code) {
  switch (code) {
    case 0: return 10000;   case 1: return 20000;   case 2: return 50000;
    case 3: return 100000;  case 4: return 125000;  case 5: return 250000;
    case 6: return 500000;  case 7: return 800000;  case 8: return 1000000;
    default: return 0;
  }
}

ParseResult parseLine(const String &line) {
  ParseResult r;
  if (line.length() == 0) { r.action = NONE; return r; }
  char cmd = line[0];

  switch (cmd) {
    case 'S': {                                   // set bitrate: S<n>
      if (line.length() < 2) { r.action = ERR; r.reply = BELL; break; }
      uint32_t br = bitrateForCode(line[1] - '0');
      if (!br) { r.action = ERR; r.reply = BELL; break; }
      r.action = SET_BITRATE; r.bitrate = br; r.reply = CR;
      break;
    }
    case 'O': r.action = OPEN;        r.reply = CR; break;   // open normal
    case 'L': r.action = OPEN_LISTEN; r.reply = CR; break;   // open listen-only
    case 'C': r.action = CLOSE;       r.reply = CR; break;   // close
    case 'V': r.action = INFO; r.reply = String("V0101") + CR; break;
    case 'N': r.action = INFO; r.reply = String("NCTK1") + CR; break;

    case 't': case 'T': case 'r': case 'R': {     // transmit frame
      bool ext = (cmd == 'T' || cmd == 'R');
      bool rtr = (cmd == 'r' || cmd == 'R');
      int idDigits = ext ? 8 : 3;
      uint32_t id = 0;
      if (!hexN(line, 1, idDigits, id)) { r.action = ERR; r.reply = BELL; break; }
      int p = 1 + idDigits;
      if (p >= (int)line.length()) { r.action = ERR; r.reply = BELL; break; }
      int dlc = hexNibble(line[p]); p++;
      if (dlc < 0 || dlc > 8) { r.action = ERR; r.reply = BELL; break; }

      CanFrame &f = r.frame;
      f.id = id; f.ext = ext ? 1 : 0; f.rtr = rtr ? 1 : 0; f.dlc = (uint8_t)dlc;
      for (int i = 0; i < 8; i++) f.data[i] = 0;
      bool bad = false;
      if (!rtr) {
        for (int i = 0; i < dlc && !bad; i++) {
          uint32_t b;
          if (!hexN(line, p + i * 2, 2, b)) bad = true;
          else f.data[i] = (uint8_t)b;
        }
      }
      if (bad) { r.action = ERR; r.reply = BELL; break; }
      r.action = TX_FRAME;
      r.reply = String(ext ? 'Z' : 'z') + CR;     // transmit ack
      break;
    }
    default:
      r.action = ERR; r.reply = BELL; break;
  }
  return r;
}

String encodeFrame(const CanFrame &f) {
  String s;
  char lead = f.rtr ? (f.ext ? 'R' : 'r') : (f.ext ? 'T' : 't');
  s += lead;
  appendHex(s, f.id, f.ext ? 8 : 3);
  s += (char)('0' + (f.dlc & 0x0F));
  if (!f.rtr) for (int i = 0; i < f.dlc; i++) appendHex(s, f.data[i], 2);
  s += CR;
  return s;
}

}  // namespace slcan
