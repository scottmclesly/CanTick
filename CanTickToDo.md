# CanTick — Remote CAN Node To-Do

**What it is:** A Seeed XIAO + Seeed CAN Bus Breakout Board that taps a physical
CAN bus and bridges it over WiFi so it appears on Scottina (the Pi) as a **real
SocketCAN interface** — usable unchanged by `can-utils`, Node-RED, and Signal K.

**Project scope:** Diagnostics + *normal* CAN participation ONLY. The single
allowed TX exception is normal node behaviour (heartbeat, ACK, request/reply,
Node-RED-originated normal traffic). **No flooding, fuzzing, replay-storm, or
injection-attack primitive** — enforced in firmware, not by convention.

**Guard-rail principle (carried over from the rest of the project):** the device
must be *physically incapable* of expressing an offensive operation. The TX code
path exposes only bounded, normal-node sends.

---

## 0. Hardware — locked + must-verify

**Controller (LOCKED): XIAO ESP32-S3.**
Rationale: the Seeed board is **MCP2515 over SPI** (+ SN65HVD230 transceiver), so
the CAN peripheral is identical across C3/C6/S3 — the ESP32 native TWAI is *not*
used. The choice is therefore about not dropping frames on a busy NMEA2000 bus
while WiFi runs. S3's dual core lets one core drain the MCP2515 (interrupt-driven
SPI) into a ring buffer while the other runs WiFi/TCP; most RAM for buffering;
native TinyUSB CDC for clean USB provisioning.

- Alt v2: **C6** (WiFi 6, better in noisy-marina 2.4 GHz — but single HP core).
- Alt bench/PoC: **C3** (cheapest, single-core, WiFi 4).

**Board facts (confirmed):** MCP2515 controller (SPI), SN65HVD230 transceiver,
CANH/CANL 3-pin terminal, classical CAN only (no CAN-FD).

**Answered. The result lives in CLAUDE.md "Hardware facts", which is the
authority. This list is kept for the record only:**
- [x] **MCP2515 crystal frequency.** 16 MHz, `CANTICK_MCP_CLOCK`.
- [x] **Onboard 120 Ω termination.** Pad `P1` on the back, left open by the
      factory. The tapped bus is already terminated, thus `P1` stays open.
- [x] **MCP2515 INT.** The board routes it to no XIAO pad. The RX path is a
      polling loop on core 1 for that reason.
- [x] **SPI pin mapping.** `CS = D7`; SCK, MOSI and MISO are the XIAO defaults.

---

### Carry-forward from the front panel effort

Both are open. Neither blocks the S3, and neither is to be acted on now.

- [ ] **The C6 is unflashed and untested.** Every panel build so far ran on the
      S3. The C6 is gated on two things: the HAT solder-joint reflow, and the
      WiFi-versus-NeoPixel reset fault, which is still open. The status pixel
      backend and the `CANTICK_STATUS_ON_MATRIX` flag are written and they
      compile for the C6, but nothing has run on that board.
- [ ] **The S3 antenna question, before any decision to drop the C6 variant.**
      Answer on the bench whether this board has a working onboard antenna or
      expects the external u.FL whip. Test at the real distance from the Pi, in
      the mounted enclosure, not bench-adjacent. The C6 was carried as the
      alternative for a noisy 2.4 GHz environment, thus the antenna result is
      the input to keeping or dropping it.

---

## 1. Locked decisions (reference)

| # | Decision | Locked value |
|---|---|---|
| 1 | Transport (v1) | SLCAN ASCII (LAWICEL) over TCP; CanTick = client, Pi = server |
| 2 | Pi-side interface | `socat` → `slcand` → `slcanN`, supervised + auto-reconnect, down on CAN-page close |
| 3 | Discovery | CanTick dials `scottina.local` (mDNS); AP-gateway `192.168.42.1` fallback |
| 4 | Provisioning | USB CDC (VID `0x303A`); kilodash pushes primary SSID/PSK + fixed fallback-AP creds → NVS |
| 5 | AP fallback | No uplink → WPA2 AP on onboard `wlan0`, same fixed creds; up while CAN page open |
| 6 | Bitrate / ID | Default 250 kbps (N2K), configurable; 29-bit extended + 11-bit |
| 7 | CAN-FD | Out of scope (MCP2515 = classical CAN) |
| 8 | TX policy | Normal node only: single-frame, heartbeat/request, Node-RED normal traffic. No flood/fuzz/replay-storm |
| 9 | Listen-only | Default RX+TX; config flag for pure-sniff |
| 10 | Overflow | INT-driven read, ring buffer, drop-oldest + dropped-frame counter to Pi |
| 11 | Timestamps | Pi-side arrival (v1); device-side optional (v2) |
| 12 | Health | Device status LED + Pi-side online/stale indicator |
| 13 | Power (remote) | USB-C 5V (boat rail or buck from N2K 12V); not parasitic off signal |
| 14 | FW update | USB reflash (v1); OTA optional later |
| 15 | Security | WPA2 + LAN isolation; plaintext SLCAN acceptable on private net (TLS = later hardening) |

**v2 upgrade path (documented, not built yet):** replace SLCAN-over-TCP with
**cannelloni over UDP** — binary framing, 1:1 `struct can_frame` mapping, lower
latency/jitter, purpose-built for remote SocketCAN. Firmware transport layer
should be abstracted so this drops in without touching CAN or WiFi code.

---

## 2. Firmware — CAN layer (MCP2515)

- [ ] Bring up MCP2515 over SPI at the verified crystal freq; set 250 kbps default.
- [ ] Enable **interrupt-driven RX** on the INT pin (no polling).
- [ ] Support 29-bit extended **and** 11-bit IDs; pass RTR/DLC through faithfully.
- [ ] Ring buffer between the CAN ISR/task and the network task; **drop-oldest**
      on overflow and increment a `dropped` counter.
- [ ] Listen-only mode (MCP2515 config) behind a config flag (mirrors the
      autodetect listen-only pattern already used in `canbus.py`).
- [ ] TX surface is **bounded**: `sendSingleFrame(id, ext, data)`,
      periodic heartbeat, and request/reply. **No** arbitrary-rate blast/replay
      API exists in the firmware at all (safety-in-code).

## 3. Firmware — transport (SLCAN over TCP, v1)

- [ ] Implement the SLCAN/LAWICEL ASCII command set needed by `slcand`:
      open/close, bitrate select, transmit (`t`/`T` for std/ext), and RX frame
      emit. Keep it to the subset `slcand` actually drives.
- [ ] TCP **client**: on WiFi-up, resolve `scottina.local` (fallback
      `192.168.42.1`), connect to the fixed CanTick port, then stream SLCAN.
- [ ] Abstract a `Transport` interface (open/read/write/close) so cannelloni/UDP
      can replace SLCAN/TCP later without touching CAN or WiFi layers.
- [ ] Reconnect with backoff on socket drop; flush/park the ring buffer while
      disconnected (bounded, drop-oldest).

## 4. Firmware — WiFi, provisioning, NVS

- [ ] NVS schema: `primary_ssid`, `primary_psk`, `fallback_ssid`, `fallback_psk`,
      `bitrate`, `listen_only`, `device_name` (`cantick-<mac>`).
- [ ] Boot logic: try primary network → on fail, try fallback AP → retry loop.
- [ ] **USB provisioning handshake** over CDC serial: framed protocol with magic
      header + checksum; accept `SET_CREDS`, `SET_BITRATE`, `GET_STATUS`; store to
      NVS; **never echo PSKs back**. Only act on framed commands, never on
      arbitrary serial noise.
- [ ] mDNS: advertise `device_name`; advertise a `_cantick._tcp` service so the
      Pi can enumerate multiple units.
- [ ] Status LED states: booting / connecting-WiFi / connected-no-Pi /
      streaming / listen-only / error.

## 5. Pi side — kilodash integration

- [ ] **Detect CanTick on USB** by VID `0x303A` (reuse the hotplug pattern in
      `devices.py`); optionally match a custom USB product string "CanTick".
- [ ] On detect: open CDC serial, run the provisioning handshake — push the Pi's
      **current** WiFi SSID/PSK (from wpa_supplicant/NetworkManager) **plus** the
      fixed fallback-AP creds. Confirm via `GET_STATUS`.
- [ ] **Interface manager** (open on CAN page, close on leave):
  - [ ] Start `socat TCP-LISTEN:<port>,reuseaddr PTY,link=/dev/cantick0,raw,echo=0`.
  - [ ] `slcand -o -c -s<bitrate-code> /dev/cantick0 slcan0` → `ip link set slcan0 up`.
  - [ ] Supervise both; auto-restart on CanTick reconnect; tear down on page exit.
  - [ ] Map multiple devices → `slcan0/1/...` via the `_cantick._tcp` names.
- [ ] **AP fallback lifecycle:** on CAN-page open, if no uplink (no default route
      / `wlan0` down), bring up hostapd + dnsmasq on `wlan0` with the fixed
      fallback SSID/PSK and gateway `192.168.42.1`; dnsmasq answers
      `scottina.local`. Tear the AP down on CAN-page close. (Only ever AP when
      there is no station uplink → no radio conflict.)
- [ ] Health readout on the CAN screen: CanTick online/stale (heartbeat), current
      bitrate, RX frames/s, and the device-reported `dropped` counter — reuse the
      Signal K freshness-indicator style.

## 6. Node-RED + Signal K (verify, no new work expected)

- [ ] Confirm the SocketCAN CAN nodes bind `slcan0` (same as a CANable).
- [ ] Confirm Signal K / canboatjs reads `slcan0` for NMEA2000 (native CAN addon
      already built in `install-phase4.sh`).
- [ ] Document the interface name to select in the Node-RED flow guide.

## 7. Testing

- [ ] Bench: MCP2515 loopback + a second CAN node at 250 kbps; verify std + ext
      IDs round-trip through `slcan0`.
- [ ] Load: replay a captured N2K log onto the bus; confirm `dropped` stays 0 at
      expected boat-bus load; note the frames/s ceiling before drops begin.
- [ ] WiFi drop/reconnect: pull power / walk out of range; confirm `slcan0`
      recovers without a kilodash restart.
- [ ] AP fallback: boot Scottina with no uplink, open CAN page, confirm CanTick
      joins the AP and streams; close page, confirm AP tears down.
- [ ] Provisioning: factory-reset NVS, plug into USB, confirm creds land and it
      connects on next boot with no serial attached.
- [ ] **Safety:** confirm no firmware/API path can emit an unbounded frame blast;
      confirm listen-only mode truly TXes nothing (scope the transceiver).

## 8. Docs

- [ ] `README` section: what CanTick is, the SocketCAN bridge model, the wiring.
- [ ] Provisioning quick-start (plug into USB once → deploy remote).
- [ ] Scope/safety note mirroring the rest of the project.
- [ ] Record the verified crystal freq + termination decision here once known.

---

### Open questions parked for later
- Device-side hardware timestamps (needs a payload extension; v2 with cannelloni).
- OTA firmware update channel.
- Multiple simultaneous CanTicks on distinct buses (naming works; UX on the CAN
  screen needs a picker).
