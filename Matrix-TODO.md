# Matrix-TODO.md

> Scratchpad for the CANTick front panel effort. Not a source of truth.
> Expires on conclusion. Authority lives in DISPLAY.md.

Read DISPLAY.md first. It is locked. Do not change a value in it. If a phase
cannot be built as written, stop and report the conflict. Do not solve it.

Do every phase in order. Do not start a hardware phase until its gate passes.

## Phase 0 — Build and test scaffold

Goal: a host test build, and no firmware change.

- Add a `native` environment to `platformio.ini` for host tests.
- Add `env:s3` and `env:c6` if they do not exist. Keep the pin map per
  environment. Do not add a preprocessor switch that serves two pin maps in
  silence.
- Add a test runner and one trivial passing test.

Accept: `pio test -e native` passes. `pio run -e s3` and `pio run -e c6` build.

## Phase 1 — Constants

Goal: every number from DISPLAY.md in one place.

- Put each value in `include/config.h` with a `CANTICK_` prefix: matrix pin,
  geometry, preset, brightness cap, tick rate, load constant, hysteresis band,
  dwell time, strip step rates, shade fractions, pulse rates, queue depth, card
  scroll count, splash scroll count.
- Put each card RGB value in `config.h`.

Accept: no magic number for the display exists in any `.cpp` file.

## Phase 2 — Frame buffer and font

Goal: pure rendering, no hardware.

- Add `include/matrix.h` and `src/matrix.cpp`.
- Port `xyToIndex` from the calibration sketch without a change.
- Port the 3×5 column-encoded font without a change.
- Add a frame buffer and a text draw function.

Accept: host tests prove that `xyToIndex` matches `LOCKED_PRESET = 10` on a
known set of coordinates, and that a glyph lands on the expected pixels.

## Phase 3 — Load estimator

Goal: a number, not a picture.

- Take frames per second and the configured bitrate. Give the load fraction
  from DISPLAY.md §3 rule 15.
- Add the 5 % hysteresis band and the 500 ms dwell.

Accept: host tests prove that a value that crosses a threshold and comes back
inside the band does not change the state, and that a state change needs the
full dwell.

## Phase 4 — Card model and queue

Goal: card order, not card pixels.

- Model a card as a type, a text or glyph, a color and a scroll count.
- Add the queue from DISPLAY.md §6: depth 4, drop-oldest on overflow, collapse
  a duplicate type, bus error to the head, no interrupt of a running card.

Accept: host tests cover overflow, collapse, bus-error priority, and a card
that runs to the end while a new event arrives.

## Phase 5 — Strip renderer

Goal: the idle layout, into a buffer.

- Draw the two 3-row strips from DISPLAY.md §2 and §4.
- Add arrow density from the message rate, the two step rates, the shade pairs,
  the 90 % darker pair and the pulse above 90 %.
- Add the missing tooth from a failed `canlink::send()` and from a drop-counter
  increase.

Accept: host tests prove the direction of each strip, that an empty strip is
dark, and that a shade pair changes at the correct load band.

## Phase 6 — Scheduler and status backend

Goal: one tick, one `show()`, and no hardware yet.

- Add a fixed 20 Hz tick. Call the driver one time per tick, and only when the
  frame changes.
- Add the matrix output backend to `status_led`. Keep the state machine where
  it is. Do not write a second one.
- Put the driver call behind a thin interface so a fake driver serves the host
  tests.

Accept: host tests prove that an unchanged frame raises no driver call, and
that a card pauses both strips and then releases them.

Gate for Phase 7: every host test passes, and both environments build.

## Phase 7 — HARDWARE, S3 variant

Do not start until the Phase 6 gate passes. Ask Scott before you flash.

- Flash the S3. Check the splash, then a bus-speed card, then live strips.
- Check the brightness cap on the mounted enclosure.
- Confirm the orientation on the real panel.
- Record the matrix task core and priority in the CLAUDE.md runtime table.

Accept: Scott confirms each item on the bench.

## Phase 8 — HARDWARE, C6 variant

Do not start until the HAT solder joint is reflowed and Scott confirms it. The
WiFi radio and NeoPixel timing conflict is open on this board.

- Flash the C6. Check that the splash finishes before the WiFi start.
- Run a full connect cycle and watch for a reset.
- Check the `status_led` matrix backend, because the C6 has no onboard LED.

Accept: Scott confirms that no reset occurs across ten connect cycles.

## Phase 9 — Close-out

Do not run this until Scott says the trigger phrase.

Run the five beats from CLAUDE.md. Harvest every bench answer into DISPLAY.md.
Read DISPLAY.md back and confirm each one is present in full. Then delete this
file.