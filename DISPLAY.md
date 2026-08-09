# DISPLAY.md

Authority document for the CANTick front panel. Locked on 8 August 2026.

This document does not expire. It is not a scratchpad. A change to it needs the
approval of Scott McLesly.

PROTOCOL.md owns the wire. This document owns the panel. The display is local
to the device, and it crosses no wire, thus a change here does not move
`CANTICK_CONTRACT_VERSION`.

The product name in prose and on the splash is CANTick. Source identifiers keep
the lower-case form `cantick`.

## 1. Panel facts

These values come from bench calibration on the mounted enclosure. They are
locked. Do not derive them again.

| Item | Value |
|---|---|
| Part | Seeed 6×10 WS2812B RGB matrix |
| Data pin | `D0`, soldered, permanent |
| Geometry | 10 columns × 6 rows |
| Wiring order | Column-major |
| Color order | GRB |
| Orientation preset | `LOCKED_PRESET = 10`, flipY plus column-major, no zigzag |
| Font | 3×5, column-encoded |
| Index helper | `xyToIndex` from the calibration sketch |

`D0` is a `constexpr`, not a preprocessor macro. `#ifdef D0` is always false.
Write `constexpr int MATRIX_PIN = D0;`.

The 3×5 font and `xyToIndex` are the canonical rendering primitives. Do not
write a second index function.

One blank column goes between two glyphs, thus a glyph step is 4 columns.

## 2. Panel layout

The panel has two layouts. Only one is on screen at a time.

**Idle layout.** Rows 0 to 2 are the inbound strip. Rows 3 to 5 are the
outbound strip. Each strip is 3 rows high.

**Card layout.** A card takes all 6 rows. Both strips pause while a card
scrolls. Both strips resume when the card ends.

## 3. Rules

1. The device gives user feedback through cards. A card holds scrolling text or
   a glyph.
2. A card scrolls two times on the event that raises it. The splash cards are
   the one exception. See §7.
3. Each bus speed has one color. See §5.
4. A card takes all 6 rows. Both strips pause while a card scrolls.
5. A running arrow strip shows CAN traffic. The arrows alternate two shades of
   the color of the configured bus speed.
6. The inbound strip is on top and it runs right to left. The outbound strip is
   on the bottom and it runs left to right.
7. One arrow is one message.
8. The arrow density increases as the message rate increases.
9. At about 50 % bus load the strip is full. The arrows then alternate color on
   each odd and even step. No space is left for a new arrow.
10. Above 50 % load the strip speed increases.
11. At 90 % and above the arrows change to a darker alternating pair.
12. At 95 % and above the strip is a solid line, and it pulses. The pulse rate
    increases with the load.
13. A failed packet is a missing arrow shape, in black, in the place where the
    arrow goes on a success. It is a missing tooth in the stream.
14. The animation tick is fixed at 20 Hz. Call `show()` one time per tick. Do
    not call `show()` when the frame does not change. A continuous `show()`
    corrupts the signal.
15. Bus load is an estimate:
    `load = frames_per_second × 111 ÷ bitrate`. The constant 111 is the bit
    count of an 8-byte frame with stuffing. A threshold has 5 % hysteresis on
    each side, and a state change needs a 500 ms dwell.
    The dead zone is the open interval between the thresholds. Thus a rise
    needs the load at the threshold plus 5 % or more, and a fall needs the load
    at the threshold minus 5 % or less.
    The 5 % band applies to every threshold except the HIGH to PULSE edge at
    95 %. That edge has no band, and the 500 ms dwell alone holds it steady. A
    band there is wider than the gap between the two thresholds, which makes
    the 95 % to 100 % pulse ramp unreachable on a rising load.
    The load estimate has no cap at 100 %. A value above 100 % means the
    provisioned bitrate does not match the bus. That is a diagnostic finding,
    and the device does not hide it.
16. The global brightness cap is 40 of 255. It is a tunable in `config.h`.

## 4. Strip behavior

An idle strip with no traffic is dark. Do not draw a placeholder arrow.

The strip speed has two steps only:

| Load | Step rate |
|---|---|
| Below 50 % | One pixel each 4 ticks |
| 50 % and above | One pixel each tick |

*[Recommendation, not confirmed by Scott. Overrule if you want a smooth ramp.]*

The shade pairs, as a fraction of the bus-speed color:

| Load band | Shade A | Shade B |
|---|---|---|
| Below 90 % | 100 % | 40 % |
| 90 % to 95 % | 25 % | 15 % |
| 95 % and above, pulse | 100 % down to 25 % | — |

The pulse rate goes from 1 Hz at 95 % load to 4 Hz at 100 % load.

### Missing tooth

Two counters drive the missing arrow:

- A failed `canlink::send()` blanks one slot on the outbound strip.
- An increase of the drop counter blanks one slot on the inbound strip.

The drop counter counts overflow events on a rising edge. It is a lower limit
on lost frames, not an exact count. Thus a missing tooth means that loss
happened. It does not mean that one frame was lost.

*[Recommendation, not confirmed by Scott.]*

## 5. Card vocabulary

The bus speed is a configured value. Scottina Prime pushes it over `CTK1` into
NVS. The device does not measure it. There is no autobaud in this effort.

| Card | Meaning | Color | RGB |
|---|---|---|---|
| `1 Mbit/s` | Configured bus speed | Orange | `0xFF6000` |
| `500 kbit/s` | Configured bus speed | Red | `0xFF0000` |
| `250 kbit/s` | Configured bus speed | Blue | `0x0040FF` |
| `125 kbit/s` | Configured bus speed | Teal | `0x00C0A0` |
| `100 kbit/s` | Configured bus speed | Green | `0x00FF00` |
| `50 kbit/s` | Configured bus speed | Purple | `0xA000FF` |
| `X X X` | Bus error | Red, pulsing | `0xFF0000` |
| `Wi-Fi` | WiFi connected | Green | `0x00FF00` |
| `Wi-Fi` | WiFi connects now | Yellow, pulsing | `0xFFC000` |
| `Wi-Fi` | WiFi disconnected | Red | `0xFF0000` |

The RGB values are defaults. Put each one in `config.h` as a tunable. The
global brightness cap applies after the color.

The MCP2515 does classical CAN only. A CAN-FD card is not reachable, thus it is
not in this table.

### Bus error

The bus-off condition raises the `X X X` card. A successful recovery clears it.
An error-passive condition does not raise a card.

*[The clear condition is a recommendation, not confirmed by Scott.]*

### Card pulse

Two cards pulse. The bus-error card pulses at 2 Hz. The WiFi-joining card
pulses at 1 Hz.

Both cards swing between 100 % and 25 % of the card color. The strip pulse
above 90 % load uses the same range. Thus one helper serves all three.

## 6. Card queue

A card runs to the end. It is not interrupted, and it is not merged with
another card.

A new event goes to a queue in the order of arrival. The bus-error card is the
one exception: it goes to the head of the queue.

The queue holds 4 cards. On overflow it drops the oldest. Two cards of the same
type in the queue collapse to one.

A collapse keeps the older entry's position, and it takes the newer text and
color. Thus the panel never shows a state that the device has left.

An overflow drops the oldest waiting card first, and the new card then lands
under its own rule. Thus an overflow never drops the bus-error card that caused
it.

*[This rule is new. A build needs it. Change it if you want a different order.]*

## 7. Boot

The splash is two cards: `CANTick`, then `V1.0`. Each splash card scrolls one
time. Every other card scrolls two times.

The version string comes from a build macro, not from a literal in the source.
The splash and the build can then never disagree.

The splash finishes before the WiFi start on the C6. The WiFi radio conflicts
with NeoPixel interrupt timing on that board, and the reset fault is still
open. Do not run an animation through a connect attempt.

## 8. Accepted risks

The WiFi green and the 100 kbit/s green are one color. The WiFi red and the
500 kbit/s red are one color. Cards are full-panel and sequential, thus the
context separates them. Color-blind users read the text, not the color.

## 9. Code notes

Keep the display code flat and short. Do not build an abstraction layer over
the matrix. Copy the pattern that works on the bench. A previous attempt failed
through over-engineering.

The C6 has no usable onboard LED. The `status_led` state machine drives the
matrix on that variant. Keep one state machine. Add an output backend only.

Write every new number in `config.h` with a `CANTICK_` prefix.