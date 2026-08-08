#pragma once
// ── CanTick build-time configuration ─────────────────────────────────────────
// Two authority documents feed this file. Keep this file in agreement with both.
//   PROTOCOL.md  "Locked parameters"  — the wire. A change moves the contract version.
//   DISPLAY.md                        — the front panel. A change moves no version.

#define CANTICK_FW_VERSION        "0.1.0"
#define CANTICK_CONTRACT_VERSION  1

// ── CAN / MCP2515 (Seeed CAN Bus Breakout Board for XIAO) ────────────────────
// CS is confirmed by the Seeed wiki for this exact board.
#define CANTICK_CAN_CS_PIN        D7
// INT: this compact breakout likely does NOT route MCP2515 INT to a XIAO pad,
// so the default RX path is a dedicated-core polling task (see can_link.cpp).
// If you verify a pad wired to INT, define it here to enable IRQ-driven RX.
// #define CANTICK_CAN_INT_PIN    D3
#define CANTICK_MCP_CLOCK         MCP_16MHZ       // 16 MHz crystal (confirmed)
#define CANTICK_DEFAULT_BITRATE   250000UL        // NMEA2000

// ── Network endpoints (PROTOCOL.md §Locked parameters) ───────────────────────
#define CANTICK_PI_HOST           "scottina.local"
#define CANTICK_PI_FALLBACK_IP    "192.168.42.1"  // Pi AP gateway
#define CANTICK_SLCAN_TCP_PORT    29536
#define CANTICK_HEARTBEAT_UDP_PORT 29537
#define CANTICK_HEARTBEAT_MS      2000

// ── Fallback AP (overridable via provisioning; PSK is never baked in source) ─
#define CANTICK_FALLBACK_AP_SSID  "Scottina-CanTick"

// ── Storage ──────────────────────────────────────────────────────────────────
#define CANTICK_NVS_NS            "cantick"

// ── Status LED (XIAO ESP32-S3 onboard user LED, GPIO21, active-LOW) ──────────
#define CANTICK_STATUS_LED_PIN    LED_BUILTIN
#define CANTICK_STATUS_LED_ACTIVE_LOW 1

// ── Front panel: Seeed 6x10 WS2812B matrix ───────────────────────────────────
// Mirror of DISPLAY.md. That document is the authority for the panel, and it is
// locked. Change a value there first. A change here moves no contract version,
// because the display is device-local and crosses no wire.
//
// Every value below is an integer. A percent is a whole percent, thus the
// render code needs no float.

// Panel facts (DISPLAY.md §1). The pin stays a XIAO pad name, as CANTICK_CAN_CS_PIN
// does. The board variant header resolves the pad to its own GPIO.
// D0 is a constexpr, not a macro. Take the constexpr at the point of use:
//   constexpr int MATRIX_PIN = CANTICK_MATRIX_PIN;
// Never write #ifdef on this name to test for a pad.
#define CANTICK_MATRIX_PIN        D0
#define CANTICK_MATRIX_COLS       10
#define CANTICK_MATRIX_ROWS       6
#define CANTICK_MATRIX_PIXELS     (CANTICK_MATRIX_COLS * CANTICK_MATRIX_ROWS)
#define CANTICK_MATRIX_PRESET     10        // LOCKED_PRESET: flipY + column-major, no zigzag
#define CANTICK_FONT_WIDTH        3         // 3x5 font, column-encoded
#define CANTICK_FONT_HEIGHT       5

// Brightness cap (DISPLAY.md §3 rule 16). It applies after the card color.
#define CANTICK_MATRIX_BRIGHTNESS 40        // of 255

// Animation tick (DISPLAY.md §3 rule 14). Call show() one time per tick, and
// only when the frame changes. A continuous show() corrupts the signal.
#define CANTICK_MATRIX_TICK_HZ    20
#define CANTICK_MATRIX_TICK_MS    (1000 / CANTICK_MATRIX_TICK_HZ)

// Idle layout (DISPLAY.md §2). The inbound strip is on top and runs right to
// left. The outbound strip is on the bottom and runs left to right.
#define CANTICK_STRIP_ROWS        3
#define CANTICK_STRIP_IN_ROW0     0         // inbound strip, top row
#define CANTICK_STRIP_OUT_ROW0    3         // outbound strip, top row

// Load estimate (DISPLAY.md §3 rule 15):
//   load = frames_per_second x CANTICK_LOAD_FRAME_BITS / bitrate
// 111 is the bit count of an 8-byte frame with stuffing.
#define CANTICK_LOAD_FRAME_BITS   111
#define CANTICK_LOAD_HYSTERESIS_PCT 5       // band on each side of a threshold
#define CANTICK_LOAD_DWELL_MS     500       // a state change needs this dwell
#define CANTICK_LOAD_BAND_MID_PCT 50        // strip is full at this load
#define CANTICK_LOAD_BAND_HIGH_PCT 90       // darker pair and pulse start here

// Strip step rate (DISPLAY.md §4). Two steps only, no ramp.
#define CANTICK_STRIP_STEP_TICKS_SLOW 4     // below 50 % load: one pixel each 4 ticks
#define CANTICK_STRIP_STEP_TICKS_FAST 1     // 50 % and above: one pixel each tick

// Shade pairs, as a percent of the bus-speed color (DISPLAY.md §4).
#define CANTICK_SHADE_A_PCT       100       // below 90 % load
#define CANTICK_SHADE_B_PCT       40
#define CANTICK_SHADE_A_HIGH_PCT  25        // 90 % load and above
#define CANTICK_SHADE_B_HIGH_PCT  15

// Solid-line pulse above 90 % load (DISPLAY.md §4).
#define CANTICK_PULSE_MAX_PCT     100       // pulse swings from this ...
#define CANTICK_PULSE_MIN_PCT     25        // ... down to this
#define CANTICK_PULSE_MIN_HZ      1         // at 90 % load
#define CANTICK_PULSE_MAX_HZ      4         // at 100 % load

// Card queue (DISPLAY.md §6). A card runs to the end. The bus-error card goes
// to the head of the queue. Two cards of one type collapse to one.
#define CANTICK_CARD_QUEUE_DEPTH  4         // drop-oldest on overflow

// Scroll counts (DISPLAY.md §3 rule 2 and §7).
#define CANTICK_CARD_SCROLL_COUNT   2
#define CANTICK_SPLASH_SCROLL_COUNT 1

// Card pulse (DISPLAY.md §5). Two cards pulse. Both swing between
// CANTICK_PULSE_MAX_PCT and CANTICK_PULSE_MIN_PCT, the range the strip pulse
// uses, thus one helper serves all three.
#define CANTICK_CARD_PULSE_BUS_ERROR_HZ 2
#define CANTICK_CARD_PULSE_WIFI_JOIN_HZ 1

// Card colors (DISPLAY.md §5). The bus speed is a configured value that
// Scottina Prime pushes over CTK1 into NVS. The device does not measure it.
#define CANTICK_RGB_BITRATE_1M    0xFF6000  // orange
#define CANTICK_RGB_BITRATE_500K  0xFF0000  // red
#define CANTICK_RGB_BITRATE_250K  0x0040FF  // blue
#define CANTICK_RGB_BITRATE_125K  0x00C0A0  // teal
#define CANTICK_RGB_BITRATE_100K  0x00FF00  // green
#define CANTICK_RGB_BITRATE_50K   0xA000FF  // purple
#define CANTICK_RGB_BUS_ERROR     0xFF0000  // red, pulsing
#define CANTICK_RGB_WIFI_UP       0x00FF00  // green
#define CANTICK_RGB_WIFI_JOIN     0xFFC000  // yellow, pulsing
#define CANTICK_RGB_WIFI_DOWN     0xFF0000  // red

// ── Tuning ───────────────────────────────────────────────────────────────────
#define CANTICK_TX_QUEUE_LEN      64
#define CANTICK_RX_QUEUE_LEN      128
#define CANTICK_WIFI_RETRY_MS     3000
#define CANTICK_TCP_RETRY_MS      2000
#define CANTICK_TCP_BACKOFF_MAX_MS 30000    // cap for exponential reconnect backoff
#define CANTICK_EFLG_POLL_MS      50        // how often the RX task samples MCP EFLG
