# CanTick

**A remote CAN node that makes a physical CAN bus appear on a Raspberry Pi as a
real SocketCAN interface — over WiFi.**

CanTick is a Seeed **XIAO ESP32-S3** plus a **Seeed CAN Bus Breakout Board**
(MCP2515 + SN65HVD230). It taps a physical CAN bus (default: NMEA2000, 250 kbps,
29-bit) and bridges every frame over WiFi to *Scottina* (the Pi), where it shows
up as `slcan0` — usable **unchanged** by `can-utils`, Node-RED, and Signal K.

> **Scope:** diagnostics and *normal* CAN participation only. There is no
> flood / fuzz / replay / injection primitive anywhere in the firmware — the TX
> path is physically incapable of expressing an offensive operation. See
> [Safety](#safety) below and [`PROTOCOL.md §6`](PROTOCOL.md).

---

## The bridge model

```
   CAN bus (NMEA2000, 250 kbps, 29-bit)
        │  CANH / CANL
   ┌────┴──────────────────┐
   │ CanTick                │   XIAO ESP32-S3 + Seeed CAN Breakout
   │  MCP2515 (SPI, CS=D7)  │   (MCP2515 + SN65HVD230, 16 MHz xtal)
   └────┬──────────────────┘
        │  WiFi  (station on the boat LAN, or joins the Pi's AP when off-grid)
        │
        │  TCP 29536 ── SLCAN ASCII (data)   ──▶  Pi (server)
        │  UDP 29537 ── heartbeat JSON       ──▶  Pi
        │
   ┌────┴───────────────────────────────────────────────┐
   │ Scottina (Pi)                                        │
   │  socat TCP-LISTEN:29536 ─▶ /dev/cantick0 (PTY)       │
   │  slcand /dev/cantick0 slcan0 ─▶ SocketCAN "slcan0"   │
   │  can-utils · Node-RED · Signal K  bind slcan0        │
   └─────────────────────────────────────────────────────┘
```

CanTick is the **TCP client**; the Pi is the **server**. In AP-fallback mode the
Pi is the gateway at a fixed IP, so CanTick always knows where to dial and the Pi
never has to discover CanTick. The full wire contract lives in
[`PROTOCOL.md`](PROTOCOL.md) — **that document is the source of truth**; this
README is orientation.

---

## Hardware & wiring

| | |
|---|---|
| Controller | XIAO **ESP32-S3** (dual-core: one core drains CAN, one runs WiFi/TCP) |
| CAN controller | **MCP2515 over SPI**, chip-select **`CS = D7`** (GPIO44) |
| Transceiver | SN65HVD230 |
| Crystal | **16 MHz** — the firmware bit-timing is built for this |
| CAN | classical CAN only (no CAN-FD) |
| Power | USB-C 5 V (boat rail or a buck from N2K 12 V) — **not** parasitic off the signal |
| Status LED | onboard user LED (GPIO21, active-low) |

SPI (SCK/MOSI/MISO) and `CS=D7` are fixed by the breakout board. INT is **not**
assumed to be routed to a XIAO pad, so RX runs as a dedicated-core polling drain;
if you confirm an INT pad, define `CANTICK_CAN_INT_PIN` in
[`include/config.h`](include/config.h) to move to interrupt-driven RX.

### ⚠️ Must-verify on your board

- **Crystal frequency (8 vs 16 MHz).** The firmware assumes **16 MHz**
  (`CANTICK_MCP_CLOCK`). Read the silkscreen; a wrong value = wrong bitrate = no
  comms.
- **Onboard 120 Ω termination.** CanTick taps an *already-terminated* bus, so it
  must **not** add termination unless you deliberately use it as an end node.
  Find the term jumper/resistor and record its default state.

---

## Firmware layout

Everything is layered against the contract in [`PROTOCOL.md`](PROTOCOL.md), not
against the Pi's code:

| Layer | Files | Responsibility |
|---|---|---|
| `can_link` | [`can_link.cpp`](src/can_link.cpp) | MCP2515 driver; dedicated-core polling RX; **bounded single-frame TX**; overflow counting |
| `slcan` | [`slcan.cpp`](src/slcan.cpp) | LAWICEL / SLCAN ASCII codec (the subset `slcand` drives) |
| `net_transport` | [`net_transport.cpp`](src/net_transport.cpp) | WiFi station/fallback, SLCAN-over-TCP client, UDP heartbeat, drop-oldest RX ring buffer |
| `provisioning` | [`provisioning.cpp`](src/provisioning.cpp) | USB-CDC `CTK1` framed protocol → NVS |
| `nvs_store` | [`nvs_store.cpp`](src/nvs_store.cpp) | persisted config (NVS namespace `cantick`) |
| `status_led` | [`status_led.cpp`](src/status_led.cpp) | health LED state machine |

The RX task is pinned to **core 1**; WiFi/TCP/heartbeat/LED run on **core 0**. A
bounded ring buffer sits between them: on overflow it **drops the oldest** frame
and increments a counter reported to the Pi.

---

## Build & flash

Uses [PlatformIO](https://platformio.org/). Open this folder as its own
workspace, then:

```bash
pio run                 # build
pio run -t upload       # flash over the native USB-C port
pio device monitor      # 115200 baud (also the provisioning channel)
```

The native USB CDC is enabled at boot (`ARDUINO_USB_CDC_ON_BOOT=1`, VID
`0x303A`) — this is both the flash/monitor port and the port the Pi provisions
over.

---

## Provisioning quick-start

**One-time setup: provision over USB once, then deploy remotely** — CanTick joins
WiFi on its own thereafter.

1. Plug CanTick into the Pi's USB port. It enumerates as an Espressif CDC serial
   device (VID `0x303A`); kilodash detects it via the hotplug pattern.
2. kilodash opens the port and runs the `CTK1` handshake, pushing:
   - the Pi's **current** WiFi SSID/PSK (`SET_CREDS slot=primary`),
   - the fixed fallback-AP creds (`SET_CREDS slot=fallback`),
   - bitrate / listen-only (`SET_NET`),
   - then `COMMIT` (persist to NVS + connect), confirmed with `GET_STATUS`.
3. Unplug and mount CanTick on the bus. On boot it tries the primary network,
   then falls back to the Pi's `Scottina-CanTick` AP.

Framing is one `\n`-terminated line per command, CRC-16/CCITT-FALSE checked;
`ssid`/`psk` are base64. **PSKs are never echoed back.** Any line that isn't a
valid `CTK1|…` frame is ignored — arbitrary serial noise is never acted on. Full
grammar in [`PROTOCOL.md §4`](PROTOCOL.md).

---

## Status LED

| State | Pattern | Meaning |
|---|---|---|
| Booting | near-solid, brief wink | firmware just came up |
| Connecting WiFi | slow even blink (½ Hz) | joining a network / fallback AP |
| Connected, no Pi | short blip, long gap | WiFi up, no active SLCAN stream |
| Streaming | brief flash ~1 Hz | TCP up, channel open, RX+TX |
| Listen-only | fast flash | channel open, **TX disabled** |
| Fault | urgent fast blink | MCP2515 init failed (check wiring / crystal) |

---

## Pi side (reference)

Owned by kilodash's interface-manager, tied to the CAN screen's lifecycle:

```bash
# bridge the incoming TCP connection to a stable PTY
socat TCP-LISTEN:29536,reuseaddr PTY,link=/dev/cantick0,raw,echo=0 &

# turn the PTY into a SocketCAN netdev at 250 k
slcand -o -c -s5 /dev/cantick0 slcan0
ip link set slcan0 up

# now:  candump slcan0   ·   Node-RED binds slcan0   ·   Signal K reads slcan0
```

The Pi treats CanTick as **stale** if no UDP heartbeat arrives for > 6 s. The
heartbeat carries `rx`/`tx`/`drop` counters, bitrate, mode, and RSSI for the CAN
screen's freshness indicator.

---

## Safety

The guard-rail is *in code*, not by convention:

1. The CAN layer exposes **single-frame send only** — no bulk / blast / replay /
   fuzz API exists anywhere in the firmware.
2. **Listen-only** mode transmits nothing: inbound `t/T/r/R` are dropped and NAKed
   (`\a`), and the MCP2515 is held in `LISTENONLY`.
3. Provisioning acts only on CRC-valid `CTK1|` frames.
4. The UDP heartbeat is send-only — the Pi has no control channel over it.

---

## Status & roadmap

Firmware (CAN / SLCAN-over-TCP / WiFi+provisioning / status LED) is implemented
and builds for the XIAO ESP32-S3. Remaining work — Pi-side kilodash integration,
bench/load/reconnect testing, and recording the verified crystal/termination
facts — is tracked in [`CanTickToDo.md`](CanTickToDo.md).

**v2 path (not built):** swap SLCAN-over-TCP for **cannelloni over UDP** (binary,
1:1 `struct can_frame`, lower jitter). The transport is isolated in
`net_transport` so this drops in without touching the CAN or WiFi layers.
