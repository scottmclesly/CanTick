# CLAUDE.md

Guidance for Claude Code in this repository.

## What this project is

CanTick is firmware for a Seeed XIAO board with an MCP2515 CAN controller and
an SN65HVD230 transceiver. The device taps a physical CAN bus, and it sends
every frame over WiFi to a Raspberry Pi. On the Pi the bus becomes a real
SocketCAN interface (`slcan0`). A small WS2812B matrix on the front gives the
state of the device at arm's length.

CanTick is the TCP client. The Pi is the TCP server.

Two board variants are in service. See "Hardware facts". There is no custom
PCB.

## Owner

This is Scott McLesly's personal project. Every commit uses
`mclesly@gmail.com`. The `isensystech` account has no part in this project and
must never commit, push or authenticate against a Scottina repository.

The Mac global `~/.gitconfig` still defaults to the work account. Set
`git config user.useConfigOnly true` in this checkout, or set a persistent
per-repository override. Check which account authenticates before a push.

Ask the user before you flash a board.

## Place in the Scottina ecosystem

CanTick is an accessory for **Scottina** (https://github.com/scottmclesly/Scottina),
a pocket diagnostic front panel on a Raspberry Pi 5. The Pi-side package keeps
the historical working name `kilodash`. The product name is Scottina. Four
parts of the Pi side own the other half of this interface:

| Pi-side part | Duty |
|---|---|
| `kilodash/devices.py` | Hotplug detection on USB. VID `0x303A` plus a product-string match |
| `kilodash/cantick.py` | `CanTickLink` (socat + slcand), `HeartbeatListener` (read-only), `CanTickProvisioner` (CDC push), `CanTickAP` (fallback AP) |
| `kilodash/screens/canbus.py` | CAN screen. It hosts the link, the health card, provisioning and the fallback AP |
| `kilodash/screens/n2k.py` | NMEA2K screen. It hosts the link too. It does no provisioning and raises no AP |

Do not add Pi-side code to this repository. This repository holds firmware
only.

### Scope

Scottina applies one scope rule to every accessory. CanTick keeps that rule in
code, not in convention. The rule, in full:

> Diagnostics only. One exception, stated explicitly: CAN has normal TX and RX
> solely for the correct heartbeat and reply behavior that bus participation
> needs. Examples: NMEA2000 address claim, claim defense, ISO request replies.
> No injection, replay, fuzz or arbitrary-frame TX is expressible anywhere in
> the UI or in a command builder.

CanTick transmits. That is by design. Read [PROTOCOL.md §6](PROTOCOL.md) before
you touch the TX path, and read safety invariant 3 below.

## Contract first

[PROTOCOL.md](PROTOCOL.md) is the source of truth for the interface. The
firmware and the Pi side each implement one half against that document, not
against each other's code.

To change the interface, do the steps in this order:

1. Change PROTOCOL.md.
2. Increase the contract version in PROTOCOL.md and in
   `CANTICK_CONTRACT_VERSION` ([include/config.h](include/config.h)).
3. Change the firmware.
4. Tell the user that the Pi side needs the same change. The Pi pins the
   number as `CONTRACT_VERSION` in `kilodash/cantick.py`, and the heartbeat
   listener warns on a mismatch.

[include/config.h](include/config.h) mirrors the "Locked parameters" table in
PROTOCOL.md. Keep the two in agreement.

The display has its own authority document, `DISPLAY.md`. It is local to this
device, and a change to it does not move the contract version. See "LED
matrix".

## Build and flash

PlatformIO is not on the PATH. Use the full path:

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -e s3 -t upload
~/.platformio/penv/bin/pio test -e native
~/.platformio/penv/bin/pio device monitor
```

The repository has three environments: `s3`, `c6` and `native`. A bare
`pio run` builds both boards. It skips `native`, because `native` has no
firmware to build.

Select one board with `-e s3` or `-e c6`.

The pin map is not per environment, and it needs no preprocessor branch. It
stays in [include/config.h](include/config.h) in XIAO pad names: `D7` for the
CAN chip select, `D0` for the matrix. Each board variant header resolves a pad
name to its own GPIO. `D7` is 44 on the S3 and 17 on the C6, thus one visible
pin map serves both boards. Do not add a switch that makes one source file
serve two pin maps in silence.

A variant difference that is not a pin map takes a build flag in
`platformio.ini`, where it is visible per environment.
`CANTICK_STATUS_ON_MATRIX` is the one in service.

`pio run` builds. `-t upload` flashes over the native USB-C port.
`device monitor` opens the serial port at 115200 baud.

The serial port has two duties: it is the log output, and it is the
provisioning channel. The `CMakeLists.txt` files are ESP-IDF leftovers. The
build uses PlatformIO with the Arduino framework.

### Host tests

Host tests need `-e native`. The `native` environment uses Unity. Its
`build_src_filter` is an allowlist. It names each pure unit that the host build
compiles, and it excludes every other file in `src/`. No Arduino source reaches
the host compiler, thus a unit in the list must have no I/O.

`matrix.cpp` is in the list. `slcan.cpp` meets the rule, and it can join the
list when someone writes tests for it.

Each new host-tested unit adds its own entry to the filter. A unit that is not
pure then fails to link, and it does not pull Arduino source onto the host.

**Host-green is never sufficient.** The host build has no `Arduino.h`, thus it
cannot see a name that collides with an Arduino macro, a board-specific pin
name or a framework type. A pure unit can pass every host test and still fail
to compile on target. `busload::Band` is the example: `LOW` and `HIGH` are
Arduino macros, and a macro ignores a namespace.

`pio run` on both boards is the guard for that class of fault. Every phase
carries it in the accept criteria for that reason. Run it before you report a
phase complete, even when the change looks pure.

The two board environments set `test_ignore = *`. A bare `pio test` then
flashes no board.

### C6 partition table

`env:c6` sets `board_build.partitions = huge_app.csv`. `env:s3` keeps the
default table.

The default table reserves a second OTA slot. This firmware never uses that
slot. Provisioning is USB CDC, and PROTOCOL.md has no OTA path. `huge_app.csv`
gives the space to the app slot. The C6 app slot goes from 1310720 to 3145728
bytes, and flash use drops from 79.0 % to 32.9 %.

The display code needs that space. The S3 has a larger flash part, thus it
needs no change.

### Push

The remote pushes over SSH through the `github.com-personal` alias. That alias
authenticates as `scottmclesly`.

Do not run `gh` in this repository. `gh` is logged in as the work account. Do
not run an authentication command here.

## Code map

| Layer | Files | Duty |
|---|---|---|
| `can_link` | [src/can_link.cpp](src/can_link.cpp) | MCP2515 driver, RX drain task, single-frame TX, overflow count |
| `slcan` | [src/slcan.cpp](src/slcan.cpp) | LAWICEL ASCII codec, pure functions, no I/O |
| `net_transport` | [src/net_transport.cpp](src/net_transport.cpp) | WiFi, TCP client, UDP heartbeat, RX ring buffer |
| `provisioning` | [src/provisioning.cpp](src/provisioning.cpp) | USB-CDC `CTK1` frames to NVS |
| `nvs_store` | [src/nvs_store.cpp](src/nvs_store.cpp) | Persisted config, NVS namespace `cantick` |
| `status_led` | [src/status_led.cpp](src/status_led.cpp) | State machine. It drives the onboard LED on S3, and the matrix on C6 |
| `matrix` | [src/matrix.cpp](src/matrix.cpp) | WS2812B front panel: frame buffer, 3×5 font, token scroll |
| `bus_load` | [src/bus_load.cpp](src/bus_load.cpp) | Bus load estimate, hysteresis band, dwell. Pure, no clock |
| `cards` | [src/cards.cpp](src/cards.cpp) | Card model and queue. Card order, not card pixels |
| `strips` | [src/strips.cpp](src/strips.cpp) | Idle layout: two 2-row traffic strips, single-pixel marks with a decay, missing tooth |
| `panel` | [src/panel.cpp](src/panel.cpp) | 20 Hz tick, card scroll, frame-change gate, the one driver seam |
| `panel_hw` | [src/panel_hw.cpp](src/panel_hw.cpp) | NeoPixel driver, matrix task, counter deltas. The only file that touches the panel hardware |

Each `.cpp` file has a header of the same name in [include/](include/). Public
functions live in a namespace. File-local state lives in an anonymous
namespace.

## Runtime model

Task placement is deliberate. Keep it.

| Task | Core | Priority | Duty |
|---|---|---|---|
| `can_rx` | 1 | 10 | Poll the MCP2515, fill the ring buffer |
| `net` | 0 | 8 | TCP connect, SLCAN pump |
| `hb` | 0 | 3 | UDP heartbeat every 2000 ms |
| `matrix` | 0 | 4 | 20 Hz panel tick, one `show()` per changed frame |
| `led` | 0 | 1 | Blink patterns |
| `loop()` | 0 | — | USB-CDC provisioning only |

The `matrix` row is **confirmed on the S3**: core 0, priority 4, a 50 ms period,
across thirteen flashed builds with no reset and no timing fault. The C6 is
untested, and its WiFi and NeoPixel conflict is still open.

Core 1 drains the CAN controller. Core 0 runs the WiFi stack. A bounded queue
of `CANTICK_RX_QUEUE_LEN` frames sits between them. On overflow the queue drops
the oldest frame and increases a counter.

The RX path is a polling loop because the board does not route the MCP2515 INT
signal to a XIAO pad. If you find an INT pad, define `CANTICK_CAN_INT_PIN` in
[include/config.h](include/config.h) and move to interrupt-driven RX.

A WS2812B write is timing-critical, thus the matrix task runs on core 0 and
never on core 1 against the CAN drain. Its priority sits below `net`.

### Drop accounting

The heartbeat field `drop` is a sum of two independent counters:

- `canlink::dropCount()` counts MCP2515 RX FIFO overflow **events**, on the
  rising edge of the EFLG bits. It is a lower limit on lost frames, not an
  exact count. The driver gives no public flag clear.
- `net::dropCount()` counts frames that the full outbound queue discarded.

A failed bus transmission is not a drop, and it never reaches the heartbeat.
`canlink::send()` reports the failure through its return value, and
`canlink::txFailCount()` records it. That count feeds the missing tooth on the
outbound strip, and nothing else reads it.

## Safety invariants

These rules hold in code. Do not weaken them, and do not add an interface that
makes them optional.

1. `canlink::send()` is the only transmit primitive. Do not add a bulk, blast,
   replay, fuzz or periodic-flood function anywhere in the firmware. This
   invariant, not listen-only, is what holds the scope rule.
2. In listen-only mode the device transmits nothing. `canlink::send()` returns
   false, and the SLCAN layer answers `t`/`T`/`r`/`R` with BELL (`\a`). The
   MCP2515 stays in `MCP_LISTENONLY`.
3. Listen-only is a user choice, not a safety floor. Normal mode is the
   default. Scottina sources GNSS PGNs through this device: `n2k/node.py` on
   the Pi sends address claim, claim defense, ISO-request replies and five GNSS
   PGNs as SLCAN `t`/`T`. In listen-only the SLCAN layer answers BELL and the
   node cannot claim an address. Do not make listen-only the default. Do not
   remove the TX path.
4. Provisioning acts on `CTK1|` lines with a correct CRC-16/CCITT-FALSE only.
   The parser ignores all other serial input in silence.
5. The UDP heartbeat is send-only. The Pi has no control channel to CanTick.
6. `STATUS`, the log and the matrix never contain a PSK.

If a request needs a change to one of these rules, tell the user first.

## LED matrix

The front panel is a Seeed 6×10 WS2812B RGB matrix. The visual language is
locked and built, and DISPLAY.md is its authority. These values come from bench
calibration on the mounted enclosure. They are locked. Do not derive them
again, and do not change one without the user.

| Item | Value |
|---|---|
| Data pin | `D0`, soldered, permanent |
| Geometry | 10 columns × 6 rows |
| Wiring order | Column-major |
| Color order | GRB |
| Orientation preset | `LOCKED_PRESET = 10`, flipY plus column-major, no zigzag |
| Font | 3×5, column-encoded |
| Index helper | `xyToIndex` from the calibration sketch |

Rules for this subsystem:

- `D0` is a `constexpr`, not a preprocessor macro. `#ifdef D0` is always false.
  Write `constexpr int MATRIX_PIN = D0;`.
- Call `show()` on a frame change only, and one time per animation tick. A
  continuous `show()` corrupts the signal.
- The 3×5 font and `xyToIndex` are the canonical rendering primitives. Use
  them. Do not write a second index function.
- Do not build an abstraction layer over the matrix. Copy the pattern that
  works on the bench, and keep it flat and short. A previous attempt failed
  through over-engineering.
- The panel has two layouts. The idle layout is two 2-row traffic strips with
  an unlit divider row between them: rows 0 and 1 inbound, row 2 dark, rows 3
  and 4 outbound, row 5 dark. The card layout is one full-panel card. Read
  DISPLAY.md before you draw anything.

### The matrix is the status LED on C6

The C6 has no usable onboard LED. The `status_led` layer has no output on that
variant. Keep the state machine in `status_led`, and add a matrix output
backend. Do not write a second state machine in `matrix`.

The status output is not decoration on C6. It is the status LED.

### Vocabulary

The device does not invent a word for a state that Scottina already names. The
CanTick health card on the Pi names the mode as `normal`, `listen` or `closed`.
It also shows a fresh or stale badge, and a `DROP` warning. If the panel must
show one of those states, it uses the card word.

No state on the panel today is one of those states. The panel shows the
configured bus speed, the bus error and the WiFi state, and the card names none
of them. Thus the rule is in force but unexercised. Apply it when the two sets
first overlap.

DISPLAY.md §5 holds the card vocabulary.

### DISPLAY.md

[DISPLAY.md](DISPLAY.md) is the authority for the display. It is locked. It
holds the panel layout, the mark and its decay, the animation rules, the load
estimate, the card vocabulary with RGB values, the card queue, the boot
sequence and the C6 status pixel.

It also holds a "Rejected on the mounted S3" list in §4. Every design on it was
built, seen on the panel and rejected, with the measurement that killed it. Do
not reopen one without a new bench check.

Read it before you touch the display. Do not put a display decision in this
file. Record every new display decision in DISPLAY.md.

DISPLAY.md is an authority document, not a scratchpad. It does not expire.
PROTOCOL.md does not cover the display, because the display is device-local and
crosses no wire. A change to DISPLAY.md does not move the contract version.

Two rules in DISPLAY.md carry a `[Recommendation, not confirmed by Scott]`
mark: the missing tooth in §4 and the bus-error clear condition in §5. Build
against them as they are. They are decisions, not open questions. The step rate
and the card queue carried the mark until the bench confirmed both.

### Runtime placement

DISPLAY.md sets a 20 Hz animation tick. The matrix task runs on core 0 at
priority 4, below `net`. A WS2812B write is timing-critical, thus it must not
run on core 1 against the CAN drain. The runtime table holds the placement, and
the S3 confirms it. The C6 is untested.

`busload::Band` uses a `BAND_` prefix on every value. Arduino defines `LOW` and
`HIGH` as macros, and a macro ignores a namespace. A bare `LOW` in that enum
breaks every firmware file that includes `Arduino.h`, and the host test build
never sees the fault.

## Hardware facts

Both variants use the Seeed XIAO CAN Bus Expansion Board. That board carries
the MCP2515 and the SN65HVD230. The pin map and the clock are the same on both
variants.

| Item | Value |
|---|---|
| CAN board | Seeed XIAO CAN Bus Expansion Board |
| CAN controller | MCP2515 on SPI, `CS = D7` |
| Transceiver | SN65HVD230 |
| Crystal | 16 MHz (`CANTICK_MCP_CLOCK`) |
| CAN type | Classical CAN, 11-bit and 29-bit, no CAN-FD |
| Default bitrate | 250000 (NMEA2000) |
| USB VID | `0x303A` (Espressif) |
| SLCAN port | TCP 29536 |
| Heartbeat port | UDP 29537 |
| Matrix | Seeed 6×10 WS2812B on `D0`, soldered |

One difference is left between the variants:

| Item | S3 variant | C6 variant |
|---|---|---|
| Controller | XIAO ESP32-S3 | XIAO ESP32-C6 |
| Status LED | `LED_BUILTIN`, active-low | None usable. The matrix is the status output |

A wrong crystal value gives a wrong bit rate and no communication.

The expansion board has a termination pad `P1` on the back. The factory leaves
it open, and a short across it adds a 120 Ω resistor. The bus that CanTick taps
is already terminated, thus `P1` stays open. Do not tell the user to short it.

The board also has its own RX and TX indicator LEDs. The hardware drives them.
The firmware cannot control them, and they are not a status output.

## Known limits

Keep these in mind before you report a bug or write a fix.

- **RTR frames do not go out on the bus.** `slcan::parseLine()` reads `r`/`R`
  correctly, but the `mcp_can` library gives no RTR parameter on
  `sendMsgBuf()`. A remote frame goes out as a data frame.
- **Received frames always report `rtr = 0`.** `readMsgBuf()` gives no RTR
  flag, so [src/can_link.cpp:57](src/can_link.cpp#L57) sets the field to 0.
- **The heartbeat reads NVS every 2000 ms** to get the bit rate
  ([src/net_transport.cpp:145](src/net_transport.cpp#L145)).
- **The device name reads `cantick-000000`.** The firmware reads the MAC before
  it starts WiFi. This is cosmetic, and it is open. The Pi matches on the VID
  and a product string, never on a unit-unique name, thus hotplug still works.
- **C6 variant: WiFi radio activity causes a reset loop.** The radio conflicts
  with NeoPixel interrupt timing. This is open. A HAT solder-joint reflow is
  the next step. Do not add a software workaround before the joint is fixed.
- **A cold solder joint on the HAT caused earlier intermittent failures.** Rule
  out hardware before you write a fix for an intermittent fault.
- Open hardware checks stay in [CanTickToDo.md](CanTickToDo.md) §0. Record a
  result there when the user confirms it on the bench.

## Conventions

- Comment on the reason, not the mechanism. The existing comments give the
  rationale for a decision, and they point to the PROTOCOL.md section.
- Mark a safety-critical line with a `SAFETY` comment, as
  [src/can_link.cpp:99](src/can_link.cpp#L99) does.
- Put a new tunable in [include/config.h](include/config.h) with a
  `CANTICK_` prefix. Do not put a magic number in a `.cpp` file.
- Keep `slcan.cpp` free of I/O. The caller writes the reply.
- Use `taskENTER_CRITICAL(&g_mux)` for a counter that two tasks touch.
- The firmware writes plain log lines to the serial port. The Pi ignores every
  line that does not start with `CTK1|`.
- Build the specific case first. Abstract only after the pattern is clear.
- Prove a change in software before you go to the bench.

## Effort lifecycle

A document that is not a locked source of truth is a scratchpad. PROTOCOL.md,
DISPLAY.md and this file are the authority. `CanTickToDo.md` is a scratchpad.

Stamp a new scratchpad at birth. Put one line at the top:

> Scratchpad for &lt;effort&gt;. Not a source of truth.
> Expires on conclusion. Authority lives in &lt;authority document&gt;.

Only Scott declares an effort complete. A green smoke test is a signal, not a
decision. The trigger phrase is **"This is Scott McLeslie. We are done here!"**.
On that phrase, do these five steps in order. Do not skip one.

1. **Distill.** Read the scratchpad once. Separate the real conclusions from
   the dead ends. Make a clean list of the conclusions.
2. **Harvest.** Write each conclusion into the authority document as a locked
   decision, in Scott's words.
3. **Verify.** Read the authority document back. Confirm that each conclusion
   is present in full, not referenced. This step is the gate. Nothing continues
   until it passes.
4. **Purge.** Remove the scratchpad from the working set. Delete loose
   research. Move signed paperwork to an archive folder that no build reads.
5. **Declare.** State that the effort is complete.

Verify and Purge are the two steps that failed last time.

## Documentation style

Write all documentation and all comments in ASD-STE100 Simplified Technical
English:

- Use the active voice.
- Use one instruction per sentence.
- Keep a procedural sentence to 20 words, and a descriptive sentence to
  25 words.
- Use the simple present tense when possible. Do not use the `-ing` form,
  except in a technical name.
- Use a word with one meaning only. Write "make sure", not "ensure". Write
  "check", not "verify". Write "start", not "initiate".
- Keep a technical name as it is: SLCAN, listen-only, drop-oldest, heartbeat,
  ring buffer, column-major.
- Do not use marketing words or filler.

Never put a `#` comment in a bash command or a bash code block. Put the
explanation in the text before the block.