# CanTick ⇄ Scottina — Interface Contract

**Status:** v1 (SLCAN-over-TCP). **Contract version:** `1`.
**This file is the source of truth.** The XIAO firmware (`cantick/`) and the Pi
side (`kilodash/screens/canbus.py` + interface-manager) each implement their half
against *this document*, not against each other's code. Change this file first,
bump the contract version, then change both sides.

---

## Topology

```
   CAN bus (NMEA2000, 250 kbps, 29-bit)
        │  CANH/CANL
   ┌────┴─────────────────┐
   │ CanTick               │   XIAO ESP32-S3 + Seeed CAN Breakout
   │  MCP2515 (SPI, CS=D7) │   (MCP2515 + SN65HVD230, 16 MHz xtal)
   │  16 MHz xtal          │
   └────┬─────────────────┘
        │  WiFi (station on boat LAN, or joins Pi AP when off-grid)
        │
        │  TCP 29536  ── SLCAN ASCII (data) ──▶  Pi (server)
        │  UDP 29537  ── heartbeat JSON ──────▶  Pi
        │
   ┌────┴───────────────────────────────────────────────┐
   │ Scottina (Pi)                                        │
   │  socat TCP-LISTEN:29536 ─▶ /dev/cantick0 (PTY)       │
   │  slcand /dev/cantick0 slcan0  ─▶  SocketCAN "slcan0" │
   │  can-utils · Node-RED · Signal K  bind slcan0        │
   └─────────────────────────────────────────────────────┘
```

CanTick is the **TCP client**; the Pi is the **server**. Reason: in AP-fallback
mode the Pi is the gateway at a fixed IP, so CanTick always knows where to dial;
the Pi never has to discover CanTick's address.

---

## Locked parameters

| Parameter | Value |
|---|---|
| CAN controller | MCP2515 over SPI, **CS = D7** |
| MCP2515 crystal | **16 MHz** |
| Default bitrate | **250000** (NMEA2000) |
| CAN ID width | 29-bit extended **and** 11-bit standard |
| CAN-FD | not supported (classical CAN only) |
| SLCAN data port | **TCP 29536** (CanTick → Pi) |
| Heartbeat port | **UDP 29537** (CanTick → Pi, send-only) |
| Heartbeat cadence | every **2000 ms** |
| Pi discovery name | `scottina.local` (mDNS) |
| Pi fallback address | `192.168.42.1` (AP gateway) |
| Fallback AP SSID | `Scottina-CanTick` (WPA2; PSK provisioned) |
| Provisioning channel | USB-C native CDC serial (VID `0x303A`) |
| Provisioning framing | `CTK1\|…` lines, CRC-16/CCITT-FALSE |
| NVS namespace | `cantick` |
| Device name | `cantick-<mac6>` (last 6 hex of MAC) |

---

## 1. Data path — SLCAN over TCP (port 29536)

CanTick speaks the **LAWICEL / SLCAN ASCII** protocol over the raw TCP stream.
The Pi bridges that stream to a PTY with `socat`, and `slcand` turns the PTY into
the `slcan0` SocketCAN netdev. Everything downstream (`candump`, `cansend`,
Node-RED SocketCAN nodes, Signal K/canboatjs) is unchanged.

### SLCAN command subset (the only commands that must be honoured)

`slcand` drives these; CanTick must accept them and emit RX frames in the same
grammar. `<CR>` = `\r` (0x0D). OK reply = `\r`. Error reply = `\a` (BELL, 0x07).

| From Pi (`slcand`) | Meaning | CanTick reply |
|---|---|---|
| `S<n><CR>` | set bitrate by code (see table) | `<CR>` / `\a` |
| `O<CR>` | open channel, normal (RX+TX) | `<CR>` |
| `L<CR>` | open channel, **listen-only** | `<CR>` |
| `C<CR>` | close channel | `<CR>` |
| `V<CR>` | firmware/hardware version | `V<hhss><CR>` |
| `N<CR>` | serial/name | `N<xxxx><CR>` |

| Frame transmit (either direction) | Meaning |
|---|---|
| `t<iii><l><d…><CR>` | standard data frame — `iii`=3-hex ID, `l`=DLC, `d…`=`2*l` hex |
| `T<iiiiiiii><l><d…><CR>` | extended data frame — 8-hex ID |
| `r<iii><l><CR>` | standard remote (RTR) frame |
| `R<iiiiiiii><l><CR>` | extended remote (RTR) frame |

- **Pi → CanTick** `t/T/r/R` = a frame the host wants transmitted on the bus.
- **CanTick → Pi** `t/T/r/R` = a frame received from the bus.
- In **listen-only** (`L`) mode CanTick MUST silently drop any `t/T/r/R` it
  receives from the Pi (reply `\a`) — it transmits nothing. (Safety invariant.)

### Bitrate codes (LAWICEL `S` command)

| Code | Bitrate | | Code | Bitrate |
|---|---|---|---|---|
| S0 | 10 k | | S5 | **250 k** (N2K) |
| S1 | 20 k | | S6 | 500 k |
| S2 | 50 k | | S7 | 800 k |
| S3 | 100 k | | S8 | 1 M |
| S4 | 125 k | | | |

### Pi-side reference invocation (owned by the interface-manager)

```bash
# 1. bridge the incoming TCP connection to a stable PTY
socat TCP-LISTEN:29536,reuseaddr PTY,link=/dev/cantick0,raw,echo=0 &

# 2. turn the PTY into a SocketCAN netdev at 250 k, opened on start
slcand -o -c -s5 /dev/cantick0 slcan0
ip link set slcan0 up

# now: candump slcan0   ·   Node-RED binds slcan0   ·   Signal K reads slcan0
```

The interface-manager **supervises** both processes: on CanTick WiFi drop the TCP
connection closes; the manager tears down `slcan0` and relaunches the pair so the
next dial-in re-establishes cleanly. Lifecycle is tied to the CAN screen — start
on page open, stop on page close (same pattern as the AP, below).

> **v2 path (not built):** swap SLCAN/TCP for **cannelloni/UDP** (binary, 1:1
> `struct can_frame`, lower jitter). The firmware transport is abstracted behind
> `net_transport.*` so this drops in without touching the CAN or WiFi layers.

---

## 2. Health path — UDP heartbeat (port 29537, send-only)

Kept **separate from the SLCAN stream** so telemetry never corrupts the data
path. CanTick sends one UDP datagram to the Pi every 2 s. This drives the CAN
screen's freshness indicator (same idea as the Signal K heartbeat). It is
**status only** — the Pi never sends anything back on this port, so it is not a
control channel and carries no injection risk.

Datagram = one line of compact JSON:

```json
{"v":1,"name":"cantick-a1b2c3","fw":"0.1.0","up":12345,
 "bitrate":250000,"mode":"normal","rx":4210,"tx":3,"drop":0,"rssi":-58}
```

| Field | Meaning |
|---|---|
| `v` | contract version (`1`) |
| `name` | device name `cantick-<mac6>` |
| `fw` | firmware version |
| `up` | uptime seconds |
| `bitrate` | active CAN bitrate |
| `mode` | `normal` \| `listen` \| `closed` |
| `rx` / `tx` | frames received / transmitted since boot |
| `drop` | frames dropped (outbound queue full or MCP overflow) |
| `rssi` | WiFi RSSI dBm |

Pi treats CanTick as **stale** if no datagram arrives for > 6 s (3 missed).

---

## 3. Discovery

1. CanTick resolves **`scottina.local`** via mDNS and dials `:29536`.
2. If mDNS fails (typical in AP-fallback mode), it dials the fixed AP gateway
   **`192.168.42.1:29536`**.

That's the whole discovery story. There is **no** `_cantick._tcp` advertisement
(CanTick is a client, not a server — advertising a service it doesn't host would
be wrong). The Pi learns CanTick's identity from the UDP heartbeat instead.

---

## 4. Provisioning path — USB CDC, `CTK1` framing

When CanTick is plugged into the Pi's USB port it enumerates as a native CDC
serial device (Espressif VID `0x303A`). kilodash detects it (hotplug pattern in
`devices.py`), opens the port, and runs this handshake to store WiFi credentials
and settings into NVS. **This is a one-time setup**: provision over USB once, then
deploy CanTick remotely on the bus — it connects over WiFi thereafter.

### Line framing

One command per line, `\n`-terminated:

```
CTK1|<CMD>|<key>=<val>|<key>=<val>|…|CRC=<hhhh>
```

- Fields are `|`-separated; each is `key=val`.
- `ssid` and `psk` values are **base64** (raw bytes) to survive any characters.
- `CRC` is the **last** field: CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`,
  no reflection, xorout `0x0000`) over every byte **before** `|CRC=`.
- CanTick **ignores any line** that doesn't start with `CTK1|` or fails CRC —
  arbitrary serial noise or debug logs are never acted on.

### Commands

| Direction | Command | Fields |
|---|---|---|
| Pi → CanTick | `SET_CREDS` | `slot=primary\|fallback`, `ssid=<b64>`, `psk=<b64>` |
| Pi → CanTick | `SET_NET` | `bitrate=<int>`, `listen_only=0\|1` |
| Pi → CanTick | `GET_STATUS` | — |
| Pi → CanTick | `COMMIT` | — (persist to NVS, (re)connect WiFi) |
| CanTick → Pi | `ACK` | `cmd=<name>` |
| CanTick → Pi | `NAK` | `cmd=<name>`, `err=crc\|badfield\|nvs` |
| CanTick → Pi | `STATUS` | `name=…`, `fw=…`, `wifi=connected\|down`, `ip=…`, `prov=0\|1` |

`STATUS` **never** includes any PSK.

### NVS schema (namespace `cantick`)

| Key | Type | Default |
|---|---|---|
| `pri_ssid` | str | — |
| `pri_psk` | str | — |
| `fb_ssid` | str | `Scottina-CanTick` |
| `fb_psk` | str | — (must be provisioned) |
| `bitrate` | u32 | `250000` |
| `listen` | u8 | `0` |
| `name` | str | auto `cantick-<mac6>` |

---

## 5. AP fallback (Pi side)

When the CAN screen opens and the Pi has **no uplink** (no default route /
`wlan0` down), kilodash hosts a WPA2 AP on **onboard `wlan0`** with SSID
`Scottina-CanTick`, gateway `192.168.42.1`, and DHCP via dnsmasq; dnsmasq also
answers `scottina.local`. CanTick — provisioned with the matching fallback PSK —
joins it. The AP is torn down when the CAN screen closes.

Because the Pi only becomes an AP when there is **no** station uplink, `wlan0` is
free and there's no AP/station radio conflict. When the boat LAN *is* present,
CanTick and the Pi are both stations on it and no AP is created.

---

## 6. Safety invariants (must hold in firmware)

1. The CAN layer exposes **single-frame send only**. There is no bulk/blast/
   replay/fuzz API anywhere in the firmware. TX is limited to normal node
   participation (heartbeat/ACK/request-reply/Node-RED-originated normal frames).
2. **Listen-only** mode (`L`) transmits nothing; inbound `t/T/r/R` are dropped.
3. Provisioning acts only on CRC-valid `CTK1|` frames; all other serial input is
   ignored.
4. The heartbeat (UDP 29537) is send-only; the Pi never controls CanTick over it.

---

## 7. Changing this contract

Bump **contract version** for any change to: ports, framing, SLCAN subset,
heartbeat schema, discovery, or NVS schema. CanTick reports its contract version
in the heartbeat (`v`); the Pi warns if it doesn't match what it expects.

### Appendix — CRC-16/CCITT-FALSE reference

```python
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc
```
