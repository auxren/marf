# 10. Troubleshooting & LED signals

## What the LEDs are telling you

| Signal | Meaning |
|--------|---------|
| Step LEDs **flash downward** | A program was just **saved**. |
| Step LEDs **flash upward** | A program was just **loaded**. |
| Step LEDs **triple‑flash** (quick) | A **load was refused** — the slot is empty, corrupt, or from older firmware. Nothing was loaded. |
| Step LEDs flashing during a **Clear hold** | The program is being **cleared**. |
| Run/Wait/Stop LEDs **cycling** at power‑up | **Calibration mode** (you held Stage Address 1 Advance at boot). |
| Pulse LEDs **toggled** | You are in **Save** or **Load** mode. |
| **Display LEDs blinking** and the module unresponsive, then it resets itself | A firmware **fault was caught**. The watchdog automatically restarts the module. If this recurs, please file an issue with what you were doing. |

## Common issues

**A slider does nothing after loading a program.**
Loaded sliders are *pinned* until you move them through their stored value. Move
the slider (min‑to‑max sweep always works) to free it. See
[Slider pinning](07-saving-and-loading.md#slider-pinning-after-a-load).

**Pitch is off / doesn't track across the range.**
Recalibrate ([Calibration](03-calibration.md)) and confirm the V/oct
[DIP switches](04-front-panel-reference.md#dip-switches) match your system
(1 V, 1.2 V or 2 V per octave).

**My saved programs disappeared after updating the firmware.**
This firmware uses a new, checksummed storage format and does not read programs
or calibration written by older firmware (they are safely ignored, not
mis‑loaded). **Recalibrate and re‑save** your programs after updating.

**Panel controls or external inputs behave erratically.**
The module relies on calibration to read its controls. If you have never
calibrated this firmware, or calibration data was lost, the module falls back to
a safe uncalibrated scaling — run [Calibration](03-calibration.md).

**A stage's quantized pitch lands on the "wrong" note.**
Quantize currently snaps to the nearest **semitone** (full chromatic). It rounds
to the closest semitone for the active V/oct; check the octave/range switches if
the octave is unexpected.

**Pulses don't reliably reach several destinations at once.**
See [Stacking pulses](09-pulse-tricks.md#stacking-pulses-to-multiple-destinations).

## Recovery

- **Recalibrate** to fix control/input scaling (this erases saved programs).
- **Clear** (hold the Clear switch) to reset all stage programming to defaults.
- **Reflash** the firmware if the module misbehaves after an interrupted update
  ([Installation](02-installation-and-flashing.md)).

## Hardware modifications

Several worthwhile hardware improvements (including the pulse‑input impedance
change) are described on
[Dave Brown's 248 page](https://modularsynthesis.com/roman/buchla248/248_mods.htm).
