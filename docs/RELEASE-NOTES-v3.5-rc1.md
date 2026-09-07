# MARF v3.5-rc1 — release candidate

A release candidate, not a release. Two things want testing, and one of them
touches where your presets live.

## Which file do I flash?

| File | For |
|---|---|
| `marf-…-<date>.hex` (no suffix) | v2 boards. **Start here.** |
| `marf-…-REV1.hex` | original v1.x boards (REV1) |
| `marf-…-v2-200e-bus.hex` | v2 boards **that have had the two-wire 200e bus modification** |

The `200e-bus` image is only useful if you have done the hardware modification
in `docs/200e-BUS-WIRING.md`. On an unmodified module it behaves like the normal
image but carries code you cannot use, so flash the normal one. It is
deliberately not offered as a one-click flasher, to make it hard to pick by
accident. There is no REV1 bus image: that pin mapping has never been verified
for continuity on v1 hardware.

If you would rather not deal with a programmer, `MARF-Flasher-V2.zip` and
`MARF-Flasher-REV1.zip` carry the same two normal images with a one-click
installer. The bus image is `.hex` only, on purpose.

## New

- **30 preset slots, up from 16.** Slots 17–30 are yours alone: the factory bank
  never writes there. On the step LEDs a slot in the upper bank blinks, the same
  convention a section-shifted generator already uses.
- **Buchla 200e preset bus support** (needs the hardware modification). A preset
  manager can save and recall the module along with the rest of the case. Two
  jumpers onto the TO COMPUTER header plus two wires to the power connector; no
  soldering at the module itself.

## Changed

- The external DAC writes in the AFG tick handlers now happen just outside the
  interrupt-masked section instead of inside it. Same values, same order, and it
  shortens the masked window — but it is a real change in the CV output path and
  is the thing to listen for if something feels different.

## Fixed

- A corrupt preset-bus frame could be re-read as a different command and
  silently overwrite a preset slot. Frame lengths are now validated.

## Known issues and limits

- **The bus work has been tested on exactly one module, in one case, on one
  bus.** That is the main reason this is an RC.
- Bus traffic **has** been tested with a sequence loaded and the module's timing
  interrupts running (80/80 recalls, no errors), but not while actively driving
  clocks and pulses into a patch. If you run the bus mid-performance and
  something misbehaves, that is a genuinely useful report.
- A preset manager will **not list** the MARF. Recall and save work regardless —
  they are broadcasts — but the module does not announce itself.
- Storage-card backup and restore over the bus is **not implemented**.
- 200e bus support is v2 only.

## What to check, in order of how much it would matter

1. **Your existing presets still load.** The EEPROM layout grew from 16 slots to
   30. Slots 1–16 keep their exact addresses and presets saved by 3.4 or earlier
   should load unchanged — but this is the change that could cost you work, so
   please check it first and report anything odd immediately.
2. Saving and loading across the full 1–30 range, including the blinking
   upper-bank display.
3. That the module still sounds and behaves as it did — particularly CV output,
   given the DAC timing change above.
4. If you have done the modification: recall and save from your preset manager.

## Measurements behind the bus claim

Taken on a 200e case with a Studio H WPM, a 259e, a 251e and a Studio H CSR:

| | |
|---|---|
| Command frames intact | 586 / 586 |
| Preset recalls decoded and applied | 192 / 192 |
| Preset saves | 16 / 16, plus a verified round trip |
| Recalls with a sequence loaded and running | 80 / 80 |
| Bus transaction timing | 592–616 µs, versus 595–616 µs with the module not fitted |

The last row is the one worth understanding: the module's presence is not
visible in the bus timing at all.
