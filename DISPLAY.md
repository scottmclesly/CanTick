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

The orientation is confirmed on the mounted S3. Text reads upright and left to
right on `LOCKED_PRESET = 10`.

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

The font is the ported set plus a period. The splash draws the version from
`CANTICK_FW_VERSION`, and that macro holds two periods, thus the panel needs
the glyph. Every ported glyph stays byte-identical to the calibration sketch.

## 2. Panel layout

The panel has two layouts. Only one is on screen at a time.

**Idle layout.** Rows 0 and 1 are the inbound strip. Row 2 is the divider. Rows
3 and 4 are the outbound strip. Row 5 stays dark. Each strip is 2 rows high.

The divider is unlit. The gap alone separates the two directions. A bench check
on the mounted S3 set this: a lit bar spends light and adds nothing that the
gap does not already say.

**Card layout.** A card takes all 6 rows. Both strips pause while a card
scrolls. Both strips resume when the card ends.

A card scrolls one column each 2 ticks. A bench check on the mounted S3 set
this rate: one column each tick is too fast to read at arm's length.

Card text draws at row 0. The font is 5 rows, thus row 5 stays free.

## 3. Rules

1. The device gives user feedback through cards. A card holds scrolling text or
   a glyph.
2. A card scrolls two times on the event that raises it. The splash cards are
   the one exception. See §7.
3. Each bus speed has one color. See §5.
4. A card takes all 6 rows. Both strips pause while a card scrolls.
5. A running strip of marks shows CAN traffic, in the color of the configured
   bus speed.
6. The inbound strip is on top and it runs right to left. The outbound strip is
   on the bottom and it runs left to right.
7. One mark is one message.
8. The mark density reads the message rate at very low load only. The strip
    steps 20 times each second, and a mark takes 4 steps, thus the strip draws
    5 marks each second at most. Five frames each second is 555 bit/s, which is
    about 0.1 % of a 500 kbit/s bus and about 0.2 % of a 250 kbit/s bus. Above
    that load the strip is full, and the density carries nothing more.
9. Load is carried by mark density and step rate only. The mark's decay is
   identical at every load. No band changes a shade.
10. A failed packet is a missing mark, in black, in the place where the mark
    goes on a success. It is a missing tooth in the stream.
11. The animation tick is fixed at 20 Hz. Call `show()` one time per tick. Do
    not call `show()` when the frame does not change. A continuous `show()`
    corrupts the signal.
12. Bus load is an estimate:
    `load = frames_per_second × 111 ÷ bitrate`. The constant 111 is the bit
    count of an 8-byte frame with stuffing. A threshold has 5 % hysteresis on
    each side, and a state change needs a 500 ms dwell.
    The dead zone is the open interval between the thresholds. Thus a rise
    needs the load at the threshold plus 5 % or more, and a fall needs the load
    at the threshold minus 5 % or less.
    The 5 % band applies to every threshold except the HIGH to PULSE edge at
    95 %. That edge has no band, and the 500 ms dwell alone holds it steady.
    The 90 % and 95 % thresholds sit 5 % apart, thus a 5 % band on each side
    would overlap them.
    The load estimate has no cap at 100 %. A value above 100 % means the
    provisioned bitrate does not match the bus. That is a diagnostic finding,
    and the device does not hide it.
13. The global brightness cap is 20 of 255. It is a tunable in `config.h`. A
    bench check on the mounted S3 set this value.
    The floor is 11. Below that the last decay column computes to zero, and the
    4-column mark becomes a 3-column mark in silence.

## 4. Strip behavior

An idle strip with no traffic is dark. Do not draw a placeholder mark.

One message is one mark, and one mark is a single pixel. It has no shape. It
takes one column and one row.

A mark carries a 4-column decay, as a fraction of the bus-speed color:

| Column | Shade | `config.h` |
|---|---|---|
| Head | 100 % | `CANTICK_DECAY_HEAD_PCT` |
| 1 behind | 50 % | `CANTICK_DECAY_1_PCT` |
| 2 behind | 25 % | `CANTICK_DECAY_2_PCT` |
| 3 behind | 10 % | `CANTICK_DECAY_3_PCT` |

The last column needs a brightness cap of 11 or more to compute above zero. A
cap below that turns the 4-column mark into a 3-column mark in silence.

A message picks one of its strip's 2 rows at random. Its decay stays on that
row, thus a mark never smears across both rows.

Marks arrive clumped, not evenly spaced. Runs and gaps then form on their own,
and the texture of the traffic is readable without any extra rule.

Traffic draws in the bus-speed color. There is no heat ramp, and the decay
takes no separate color of its own.

The traffic color carries the bus speed. The divider is unlit, thus the color
of the marks is the only place the speed shows on the idle layout.

The direction is read from motion. The head is the brightest column and the
decay trails away from the travel, thus a still frame names the direction with
no motion at all. A host test holds that property.

The strip speed has two steps only:

| Load | Step rate |
|---|---|
| Below 50 % | One pixel each 4 ticks |
| 50 % and above | One pixel each tick |

The renderer reads the load band in one place only: this step-rate interval, at
the 50 % boundary. Four bands compute, and two pictures exist. Below 50 % the
strip steps slowly, and at 50 % and above it steps once each tick. The HIGH and
PULSE bands are retained for a future rule. They are not visible today.

Nothing in the renderer reads the load band for a shade. A single pixel with a
gradient has no shade left to give: every attempt to take some either inverts
the mark or eats its tail. The density and the step rate carry the load on
their own. At 92 % load the strip holds 9.6 of its 10 columns, which is
saturation showing itself with nothing dimming.

### Missing tooth

Two counters drive the missing mark:

- A failed `canlink::send()` blanks one slot on the outbound strip.
- An increase of the drop counter blanks one slot on the inbound strip.

The drop counter counts overflow events on a rising edge. It is a lower limit
on lost frames, not an exact count. Thus a missing tooth means that loss
happened. It does not mean that one frame was lost.

A missing tooth stays black on a full strip, because loss must stay visible in
the condition that produces it.

*[Recommendation, not confirmed by Scott.]*

### Rejected on the mounted S3

Each of these was built, seen on the panel and rejected. Do not reopen one
without a new bench check.

- The comet mark: a full-shade head column and one trailing column at 40 %,
  filling the rows of its strip.
- The heat ramp: a mark color that follows the load instead of the bus speed.
- The inverse-lit divider: full bus color at idle, dimming as the load rises.
- The shaped dividers: a bar eaten from the middle, and a bar retreating to a
  centre point.
- A fixed traffic color: one hue for every bus speed, in place of the
  bus-speed color.

Three readings of the high bands were built and measured on the mounted S3.
All three are rejected, and the bands now change no shade at all.

- R1, the band acting on the head alone: the head drops to the darker shade
  while the decay holds, thus the first decay column outshines the head. The
  mark inverts, and an inverted mark reads as a fault.
- R2, the band scaling the whole mark: the 10 % tail column falls to 2 % of
  full, which is 0 at any brightness. The 4-column mark silently becomes 3.
- R3, the band carried by the arrival rate: the pulse period at 97 % load is
  0.5 s and a mark crosses the strip in 0.5 s, thus the strip averages the
  modulation away. The measured swing is 0.1 %. The mean rate also falls, so a
  saturated bus paints 5.2 lit columns against 9.6 at 92 %, and saturation
  reads as calm.

## 5. Card vocabulary

The bus speed is a configured value. Scottina Prime pushes it over `CTK1` into
NVS. The device does not measure it. There is no autobaud in this effort.

| Card | Meaning | Color | RGB |
|---|---|---|---|
| `1M` | Configured bus speed | Orange | `0xFF6000` |
| `500K` | Configured bus speed | Red | `0xFF0000` |
| `250K` | Configured bus speed | Blue | `0x0040FF` |
| `125K` | Configured bus speed | Teal | `0x00C0A0` |
| `100K` | Configured bus speed | Green | `0x00FF00` |
| `50K` | Configured bus speed | Purple | `0xA000FF` |
| `X X X` | Bus error | Red, pulsing | `0xFF0000` |
| `Wi-Fi` | WiFi connected | Green | `0x00FF00` |
| `Wi-Fi` | WiFi connects now | Yellow, pulsing | `0xFFC000` |
| `Wi-Fi` | WiFi disconnected | Red | `0xFF0000` |

The bus-speed card text is short because the font cannot draw `/`, and at ten
columns the full string is a long scroll for a value the card color already
carries.

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

Both cards swing between 100 % and 25 % of the card color. The strip does not
pulse, thus the pulse belongs to the cards alone.

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

## 7. Boot

The splash is two cards: `CANTick`, then `V` plus `CANTICK_FW_VERSION`. Each
splash card scrolls one time. Every other card scrolls two times.

The version string comes from a build macro, not from a literal in the source.
The splash and the build can then never disagree.

The splash color is green, `0x00FF00`. A bench check on the mounted S3 set this
value: `0x00FF66` carries 40 % blue, which reads as teal on the panel and
collides with the 250K teal card. The splash runs before any other card exists,
thus its position makes it unambiguous.

The splash finishes before the WiFi start on the C6. The WiFi radio conflicts
with NeoPixel interrupt timing on that board, and the reset fault is still
open. Do not run an animation through a connect attempt.

## 8. Accepted risks

Several elements share a color, and the traffic is now one of them:

- The 100 kbit/s green is the WiFi-connected card green and the `STREAMING`
  status pixel green.
- The 500 kbit/s red is the bus-error card red and the fault-latch red.

The traffic is continuous, thus a shared color is no longer separated by
sequence alone. The risk is accepted because position and motion separate the
three: traffic moves along a strip, a card owns the whole panel and nothing
else shows while it scrolls, and the status pixel is one fixed corner. A
color-blind user reads the card text and the motion, not the hue.

## 9. Code notes

Keep the display code flat and short. Do not build an abstraction layer over
the matrix. Copy the pattern that works on the bench. A previous attempt failed
through over-engineering.

The C6 has no usable onboard LED. The `status_led` state machine drives the
matrix on that variant. Keep one state machine. Add an output backend only.

Write every new number in `config.h` with a `CANTICK_` prefix.

## 10. Status pixel

The C6 has no usable onboard LED, thus the matrix is the status output on that
variant. The status pixel reproduces the LED literally. It is one pixel, not a
card, and it draws no word. Thus it invents no vocabulary, and the §5 rule
holds untouched.

The pixel sits at panel coordinate (9, 5). Card text holds rows 0 to 4, and the
idle layout leaves row 5 dark, thus the pixel collides with nothing in either
layout and it costs no other element a single pixel.

The `status_led` state machine and its blink patterns do not move. The backend
takes the state, the fault latch and the blink phase. It lights the pixel on
the phase, and it darkens the pixel off the phase.

| State | Color | RGB |
|---|---|---|
| `BOOTING` | White | `0xFFFFFF` |
| `WIFI` | The WiFi-joining yellow | `0xFFC000` |
| `NO_PI` | Orange | `0xFF6000` |
| `STREAMING` | The WiFi-connected green | `0x00FF00` |
| `LISTEN` | Blue | `0x0040FF` |
| fault latch | The bus-error red | `0xFF0000` |

The fault latch wins over the state.

The panel draws the status pixel last, thus nothing can paint over it. Nothing
tries: card text holds rows 0 to 4, and the idle layout leaves row 5 dark.

The S3 has an onboard LED, thus that variant draws no status pixel.
