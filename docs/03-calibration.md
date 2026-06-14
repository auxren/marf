# 3. Calibration

**Calibration is required** after flashing the firmware. Because of the
module's hardware design, the firmware needs to learn the reading of each panel
control and external input; without calibration, panel controls and external
inputs will not behave correctly.

Calibration is **two‑pass** (a high point and a low point), which lets the
firmware learn both the **range and offset** of every slider, knob and input —
so the sliders use their full travel and the inputs track accurately.

You will need a **calibrated 10 V source** (a precise, known voltage) to apply
to the external inputs (and 0 V / unpatched for the low pass).

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
   (Display LED **I** indicates the high pass.)
2. **High pass — set everything to maximum:** turn all **knobs and sliders** to
   max, and **apply a calibrated 10 V source to all four external inputs.**
3. *(Optional — pulse LED swap.)* Some hardware revisions have the Pulse 1 / 2
   LEDs reversed. If yours does, select **Pulse 2 up** to swap them (the lit LED
   moves to Pulse 2). Select **Pulse 1 up** to swap back to normal. Skip this if
   your pulse LEDs already read correctly.
4. *(Optional — pulse channel swap.)* Some units have the Pulse 1 / 2
   *switches and output jacks* reversed (the Pulse 1 switch programs Pulse 2 and
   the pulse comes out the Pulse 2 jack, etc.). If yours does, select **Time
   Source External up** to swap the pulse channels (both switches and outputs);
   select **Time Source Internal up** to leave them normal. This is independent
   of the LED swap above. Skip it if your pulse switches and jacks already match.
5. **Press Stage Address 1 *Advance*** to capture the high point. Display LED
   **II** now indicates the low pass.
6. **Low pass — set everything to minimum:** turn all **knobs and sliders** to
   min, and apply **0 V** (or unpatch) the four external inputs.
7. **Press Stage Address 2 *Advance*** to capture the low point and save.

> **This erases the entire EEPROM, including all saved programs.** Calibrate
> before you store programs you care about.

When saving, the module writes a checksummed calibration record. On every
subsequent power‑up the firmware verifies it; if the calibration data is missing
or corrupt, the module falls back to a safe uncalibrated (unity) scaling so it
still runs, but you should recalibrate for correct behaviour.

## Calibration survives firmware updates

Calibration is stored in the module's external EEPROM, **not** in the chip that
holds the firmware, so flashing new firmware does not erase it. From **3.0
onward the calibration format is frozen**: updating to any later 3.0+ build
reads your existing calibration back on boot, so **you do not need to
recalibrate after a routine firmware update.**

The module verifies the stored calibration at every power‑up. If it is missing
or in an incompatible format, the module falls back to a safe uncalibrated
(unity) scaling so it still runs — that fallback (controls suddenly tracking
wrong) is your cue that a recalibration is needed.

## When to recalibrate

- The **first time** you move onto this firmware from an older version (pre‑3.0
  / v2.66), whose calibration is in an incompatible format.
- After any of the recommended hardware modifications that affect input scaling.
- If panel controls or external inputs track incorrectly (e.g. pitch is off
  across the range) — the uncalibrated fallback described above.

A routine **3.0 → later‑3.0** update does **not** need a recalibration.
