# 3. Calibration

**Calibration is required** after flashing the firmware. Because of the
module's hardware design, the firmware needs to learn the full‑scale reading of
each panel control and external input; without calibration, panel controls and
external inputs will not behave correctly.

You will need a **calibrated 10 V source** (a precise, known voltage) to apply
to the external inputs.

## DIP switches first

Set the configuration DIP switches before calibrating
(see [DIP switches](04-front-panel-reference.md#dip-switches)):

| Switch | Setting |
|--------|---------|
| 1 | **On** for 1.2 V/octave scaling |
| 2 | **On** for 1 V/octave scaling |
| 3 | **Off** always |
| 4 | **On** if an expander module is connected |

If neither switch 1 nor 2 is on, the module uses 2 V/octave.

## Procedure

1. **Hold down Stage Address 1 *Advance*** while powering the module on. The
   module enters calibration mode — you will see the Run/Wait/Stop LEDs cycling.
2. **Turn all pots to maximum.** (Slider positions do not matter for
   calibration.)
3. **Apply a calibrated 10 V source to all four external inputs.**
4. *(Optional — pulse LED swap.)* Some hardware revisions have the Pulse 1 / 2
   LEDs reversed. If yours does, select **Pulse 2 up** to swap them (the lit LED
   moves to Pulse 2). Select **Pulse 1 up** to swap back to normal. Skip this if
   your pulse LEDs already read correctly.
5. **Press Stage Address 2 *Advance*** to save the calibration.

> **This erases the entire EEPROM, including all saved programs.** Calibrate
> before you store programs you care about.

When saving, the module writes a checksummed calibration record. On every
subsequent power‑up the firmware verifies it; if the calibration data is missing
or corrupt, the module falls back to a safe uncalibrated (unity) scaling so it
still runs, but you should recalibrate for correct behaviour.

## When to recalibrate

- After flashing or updating the firmware.
- After any of the recommended hardware modifications that affect input scaling.
- If panel controls or external inputs track incorrectly (e.g. pitch is off
  across the range).
