# REV1 (v1 hardware) compatibility contract

Goal: **changes validated on a v2 bench, plus a green `make test`, ship to
REV1 with confidence — no v1 unit on the desk.** This document defines what
that guarantee covers, what enforces it, and the (short) list of change
types that still need a real v1 unit.

Everything in here was measured on a physical B248 rev 1.0 unit with a logic
analyzer + SWD debugger during the v3.2.1/v3.2.2 bring-up. Each invariant
names its enforcement; if you change guarded code, the guard fails before the
firmware ships.

## How the guarantee works

`make test` builds and runs the full host suite **twice** — once as v2
(`MARF_HW=2`) and once as REV1 (`MARF_HW=1`):

- The **virtual bench** (`test/test_afg_bench.c`) runs the real tick engine
  (`afg.c`) in the same 1 ms windows the hardware uses, injects external
  clock pulses through the same entry points the pulse ISR uses, and asserts
  behavior: clock lock, integer ratios, gate widths, the PW0 onset trigger,
  sustain/enable holds, humanize lock-keeping. Both variants must pass the
  same assertions — engine changes cannot diverge between boards unnoticed.
- The **invariants suite** (`test/test_v1_invariants.c`) pins the
  variant-specific contracts (below) per build.
- The **compat lint** (`tools/v1_compat_lint.sh`, run by CI) blocks the bug
  pattern that historically caused v2-works/v1-breaks: reading pulse pins
  directly instead of through the accessors.

CI runs all of it on every push, and builds both firmware images.

## The V1 invariants (and their guards)

| Invariant (measured fact) | Guard |
|---|---|
| Start/Stop inputs are INVERTED on v1 (idle high, pulse pulls low); strobes read direct on both revisions. The polarity lives ONLY in the `analog_data.h` accessors. | `test_pulse_input_polarity` (both variants) + lint rule: no `GPIOB->IDR` / `GPIO_ReadInputDataBit(GPIOB` / `EXTI_GetFlagStatus` outside `analog_data.h` |
| v1 pulse pins: START1=PB7, STOP1=PB0, STROBE1=PB2, START2=PB5, STOP2=PB1, STROBE2=PB14; both-edge EXTI with leading-edge level qualification. | pin-map checks in `test_pulse_input_polarity`; qualification exercised by the bench's ISR-injection path |
| v1 ADC mux needs explicit full-chain masks (5 bytes per selection, identity indexing: 0–15 volt, 16–23 CV, 24–39 time). The table is wiring truth extracted from stock and verified per channel. | `test_v1_mux_mask_table_frozen` — FNV-64 checksum of the table; any edit fails until re-verified on hardware and re-pinned |
| The Time Multiply pot is audio-taper on v1: printed "1" ≈ calibrated 2013 (49%), printed "2" ≈ 3579, full CW ≈ 4095. Clock-follow zones are anchored to these measured marks. | zone anchors pinned in `test/test_clockfollow.c` (`x1 = 1763..2263`) |
| v1 Time/Ref DAC is 12-bit (v2: 10-bit, values >>2). | `test_time_out_scaling` per variant |
| PW0's fixed ~1 ms trigger must fire on ISR-driven (clocked) advances — the pulse-onset latch. | `test_onset_trigger_after_isr_advance` (the v3.2.2 bug, now a permanent regression test) |
| Strobe jacks on UNMODIFIED rev 1.0 boards are not wired to the MCU at all (stock had no strobe code; v1.6 was a board+firmware mod adding PB2/PB14). | documented fact — nothing to guard; REV1 firmware's PB2/PB14 path is correct for modded boards and inert on unmodified ones (PB2 is the BOOT1 strap, externally held low) |

## What still needs a real v1 unit

Be honest about the boundary. A green suite does NOT cover:

1. **New hardware surface**: touching a pin, EXTI line, DAC frame, timer, or
   the ADC scan/mux timing itself. The tests pin *current* behavior; new
   hardware interactions need a scope.
2. **Analog reality**: output levels, input thresholds, pot tapers of OTHER
   v1 units (all measurements here are from one unit; unit-to-unit taper
   spread is unknown).
3. **Timing budgets**: the v1 scan runs at half rate (discard after every mux
   move) and hot-input oversampling holds it together. Code that adds load
   to the scan loop or assumes v2 scan latency needs bench timing.
4. **The expander**, which no test or bench covers at all.

If a change stays out of those four categories — sequencing logic, modes,
UI gestures, clock-follow, Turing, presets, storage — then v2 bench
validation + `make test` is the release bar for REV1.

## Debug access if something does come up in the field

- `dbg_pulse_*` (main.c) and `dbg_cfadv[]` (afg.c) are permanent SWD taps;
  read them with openocd one-shot commands (no halt needed).
- `docs/TEST-PLAN.md` has the bench protocol; the stock rev 1.0 image is
  archived at `dist/v1-stock-backup-2026-07-12.bin` (restorable baseline).
