# 12. Troubleshooting & LED signals

## What the LEDs are telling you

| Signal | Meaning |
|--------|---------|
| Step LEDs **flash downward** | A program was just **saved**. |
| Step LEDs **flash upward** | A program was just **loaded**. |
| Step LEDs **triple‑flash** (quick) | A **load was refused** — the slot is empty, corrupt, or from older firmware. Nothing was loaded. |
| Step LEDs flashing during a **Clear hold** | The program is being **cleared**. |
| Run/Wait/Stop LEDs **cycling** at power‑up | **Calibration mode** (you held Stage Address 1 Advance at boot). |
| Pulse LEDs **toggled** | You are in **Save** or **Load** mode. |
| Mode LEDs **chase** (Quantize → Sloped → Full Range → External) | [Shift‑register mode](07-shift-register.md) was just **turned on** for the displayed sequence. |
| **Source (External) LED breathing** (slow fade) | Shift‑register mode is **on** and the focused stage is set to External (its voltage comes from a register). |
| **Display LEDs blinking** and the module unresponsive, then it resets itself | A firmware **fault was caught**. The watchdog automatically restarts the module. If this recurs, please file an issue with what you were doing. |

## Common issues

**A slider does nothing after loading a program.**
Loaded sliders are *pinned* until you move them through their stored value. Move
the slider (min‑to‑max sweep always works) to free it. See
[Slider pinning](09-saving-and-loading.md#slider-pinning-after-a-load).

**Pitch is off / doesn't track across the range.**
Recalibrate ([Calibration](03-calibration.md)) and confirm the V/oct
[DIP switches](04-front-panel-reference.md#dip-switches) match your system
(1 V, 1.2 V or 2 V per octave).

**My saved programs / calibration were ignored after updating the firmware.**
This firmware uses a checksummed, versioned storage format and ignores data
written by **older (pre‑3.0)** firmware (safely, not mis‑loaded). The first time
you move onto 3.0, **recalibrate and re‑save** your programs. Updating between
**3.0+** builds keeps your calibration (its format is frozen); only re‑save
programs if a release notes a program‑format change. Flashing never erases the
EEPROM — only running the calibration procedure does.

**Panel controls or external inputs behave erratically.**
The module relies on calibration to read its controls. If you have never
calibrated this firmware, or calibration data was lost, the module falls back to
a safe uncalibrated scaling — run [Calibration](03-calibration.md).

**A stage's quantized pitch lands on the "wrong" note.**
Quantize snaps to the nearest note in the sequence's active
[scale](06-scales.md). If notes seem to be missing, the sequence may be on a
scale other than Chromatic — hold **Quantize** and nudge a voltage slider to
check or reset the scale (a time slider sets the root), remembering scale/root
are per **displayed** sequence. Also confirm the octave/range switches and the
active V/oct if the octave itself is unexpected.

**A stage set to External is making its own evolving voltage.**
[Shift‑register mode](07-shift-register.md) is on for that sequence (its Source
LED breathes). Hold **Source External + Quantize** for ~0.8 s to toggle it back
off.

**Pulses don't reliably reach several destinations at once.**
See [Stacking pulses](11-pulse-tricks.md#stacking-pulses-to-multiple-destinations).

## Recovery

- **Recalibrate** to fix control/input scaling (this erases saved programs).
- **Clear** (hold the Clear switch) to reset all stage programming to defaults.
- **Reflash** the firmware if the module misbehaves after an interrupted update
  ([Installation](02-installation-and-flashing.md)).

## Hardware modifications

Several worthwhile hardware improvements (including the pulse‑input impedance
change) are described on
[Dave Brown's 248 page](https://modularsynthesis.com/roman/buchla248/248_mods.htm).
