#pragma once
// ── CanTick build-time configuration ─────────────────────────────────────────
// Mirror of the "Locked parameters" table in PROTOCOL.md. Keep the two in sync.

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

// ── Tuning ───────────────────────────────────────────────────────────────────
#define CANTICK_TX_QUEUE_LEN      64
#define CANTICK_RX_QUEUE_LEN      128
#define CANTICK_WIFI_RETRY_MS     3000
#define CANTICK_TCP_RETRY_MS      2000
#define CANTICK_TCP_BACKOFF_MAX_MS 30000    // cap for exponential reconnect backoff
#define CANTICK_EFLG_POLL_MS      50        // how often the RX task samples MCP EFLG
