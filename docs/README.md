# MARF — Multiple Arbitrary Function Generator

User manual for the Buchla 248r MARF running the open‑source firmware in this
repository.

> These instructions describe the behaviour of **this firmware** (v2.66 and the
> later 3.0‑line additions). Where the firmware differs from Buchla's original
> 248 manual, this manual is authoritative for the module as it actually behaves.

## Contents

1. [Overview](01-overview.md) — what the module is and its signal flow
2. [Installation & flashing](02-installation-and-flashing.md) — programming the firmware
3. [Calibration](03-calibration.md) — **required** after flashing
4. [Front‑panel reference](04-front-panel-reference.md) — every control and jack
5. [Programming a step](05-programming-steps.md) — voltage, time, pulses, modes
6. [Scales & quantizing](06-scales.md) — per‑sequence scales and roots
7. [Shift‑register (Turing) mode](07-shift-register.md) — looping per‑stage CV generators
8. [Running & clocking](08-running-and-clocking.md) — start/stop/strobe/advance, modes, loops
9. [Saving & loading programs](09-saving-and-loading.md) — the 16 memory slots
10. [Section shift](10-section-shift.md) — reaching steps 17–32 without an expander
11. [Pulse tricks](11-pulse-tricks.md) — useful pulse‑input behaviours
12. [Troubleshooting](12-troubleshooting.md) — LED signals, recovery, common issues

A combined **PDF** of this manual (`MARF-Manual.pdf`) is attached to each
[release](https://github.com/auxren/marf/releases). To build it yourself:

```sh
make manual      # requires pandoc + a LaTeX engine (tectonic)
```

## Quick start

1. Flash the firmware (see [Installation](02-installation-and-flashing.md)).
2. **Calibrate** (see [Calibration](03-calibration.md)) — the module needs this
   to read its panel controls and inputs correctly.
3. Set the [DIP switches](04-front-panel-reference.md#dip-switches) for your
   volts‑per‑octave and expander configuration.
4. Patch the **Voltage Out** of a sequence to a VCO, give a stage some sloped
   and stepped voltages, and pulse **Start**.
