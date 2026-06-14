# 4. Front‑panel reference

This chapter lists every control, input and output and what the firmware does
with it. Controls that act on *a generator* exist twice — once for **AFG 1** and
once for **AFG 2**.

## Display & edit focus

Much of the panel acts on the **currently focused stage**. Focus follows the
**display mode**:

- **View 1 / View 2** — press **Display 1** or **Display 2** to view that
  generator. Programming switches act on that generator's **currently running
  stage**, so you can reprogram on the fly as it plays.
- **Edit 1 / Edit 2** — press **Stage No Left** or **Stage No Right** while viewing to
  enter edit mode for that generator. Now you can scroll to any stage and
  program it without it having to be the playing stage.

## Stage Address section (per generator)

| Control | Firmware behaviour |
|---------|--------------------|
| **Reset** | Jumps the generator to stage 1 (the first stage). Has no effect while in continuous Stage Address mode. **Holding *both* Reset buttons (AFG 1 + AFG 2) for >1 s [randomizes the whole program](#randomize).** |
| **Pulse Select** (strobe) | Strobes: immediately jumps to the stage selected by the **Stage Address** CV input. |
| **Advance** | Manually advances one stage (equivalent to a simultaneous Start + Stop pulse). |
| **Continuous Select** | While held/engaged, the generator's stage follows the **Stage Address** CV continuously (it does not run on its own clock). Releasing returns to the previous mode. |
| **Display** | Selects this generator for viewing/editing (see above). Also used in [section‑shift](10-section-shift.md) combinations. |

## Stage No Left / Right (navigation)

- From a View mode, pressing either one enters **Edit** mode for the displayed
  generator at stage 1.
- In Edit mode they scroll the edited stage left/right; **hold** to repeat and
  scroll quickly. The selection wraps around.
- Used in [section‑shift](10-section-shift.md) and
  [save/load](09-saving-and-loading.md) selection.

## Clear switch (up / down)

A momentary switch with up and down positions:

| Action | Result |
|--------|--------|
| **Brief Clear Up** | Enter **Load** mode ([Saving & loading](09-saving-and-loading.md)). |
| **Brief Clear Down** | Enter **Save** mode. |
| **Hold Clear Up or Down** | **Clear** the program: resets all stages to defaults and stops both generators (step LEDs flash). |

## Per‑stage programming switches

These set the definition of the focused stage. "On"/"Off" pairs latch the
choice. See [Programming a step](05-programming-steps.md) for musical detail.

- **Voltage range / octave:** `0`, `2`, `4`, `6`, `8` select a limited
  (~one‑octave) range offset to that octave; **Full Range** uses the stage's
  full voltage range with no octave restriction.
- **Pulse 1 On/Off**, **Pulse 2 On/Off** — enable/disable each programmed pulse
  output on this stage.
- **Quantize / Continuous** — quantize the stage voltage to the nearest note in
  the sequence's [scale](06-scales.md) (tracking the V/oct DIP setting) or leave
  it continuous. *Holding* Quantize turns the sliders into scale/root selectors
  — see [hold combinations](#hold-combinations) below.
- **Sloped / Stepped** — glide (slew) to this stage's level, or jump to it.
- **Source External / Internal** — take the stage voltage from an external input
  (which input is chosen by the stage's voltage slider position) or from the
  stage's slider. With [shift‑register mode](07-shift-register.md) on, an
  external‑source stage reads its own register instead.
- **Stop On/Off**, **Sustain On/Off**, **Enable On/Off** — set the stage's
  operating mode (see [Running & clocking](08-running-and-clocking.md)). These
  are mutually exclusive; turning one on clears the others.
- **First On/Off**, **Last On/Off** — mark the start/end of a loop
  ([Loops](08-running-and-clocking.md#loops)).
- **Time Source External / Internal** — take the stage time from an external
  input or from the stage's time slider.
- **Time Range 1 / 2 / 3 / 4** — choose the stage's time range (see
  [Time](05-programming-steps.md#time)).

## Hold combinations

A few **switch‑held + slider** gestures reprogram the *displayed* sequence
(choose it with **Display 1 / Display 2**). While a gesture is held, the sliders
act as selectors and **do not change any stage's output**; the step LEDs show the
value, and any slider you move is pinned on release.

| Hold | Then | Does |
|------|------|------|
| **Quantize** | move a **voltage** slider | select the sequence's [scale](06-scales.md) |
| **Quantize** | move a **time** slider | select the [root](06-scales.md) |
| **Source External + Quantize** | hold ~0.8 s | toggle [shift‑register mode](07-shift-register.md) (chase animation confirms) |
| **Source External** (mode on) | move a **voltage** slider | set a stage's [clock input](07-shift-register.md#configuring-a-stage-clock--length) |
| **Source External** (mode on) | move a **time** slider | set a stage's [register length](07-shift-register.md#configuring-a-stage-clock--length) |
| **Time Source** (External, up) | move a **time** slider | set that step's **pulse width** (gate length, ~1%–99% of the step); step LEDs show a bar |

When shift‑register mode is on, the focused stage's **Source (External)** LED
**breathes** (slow fade) if that stage is set to External.

## Randomize

**Hold both Stage Address *Reset* buttons (AFG 1 and AFG 2) together for more
than one second** to randomize the entire program. Every stage gets:

- random **voltage** and **time** slider values (pinned, like a loaded program —
  move a slider through its value to take manual control again);
- a random **voltage range/octave**, and random **Quantize**, **Sloped/Stepped**
  and **Pulse 1/2** states;
- a random **time range**;

and the sequence is given a **random loop length** (a First marker on stage 1
and a Last marker on a random stage). Voltage and time **sources stay Internal**
(nothing needs to be patched) and **Stop/Sustain/Enable stay off**, so the result
plays immediately. A ~2‑second twinkling LED show runs while it happens. Both
generators reset to stage 1 afterward. (Scales/roots and shift‑register settings
are left as they were.)

## Sliders & knobs

- **Voltage slider** (per stage) — the stage's voltage level. When the voltage
  source is *external*, the slider instead **selects which external input**
  (A–D) feeds the stage.
- **Time slider** (per stage) — the stage's duration within its time range. When
  the time source is *external*, it selects an external input.
- **Time Multiply** (per generator) — scales all of that generator's stage
  times together, roughly ×0.5 to ×4.

## Inputs

- **Start / Stop / Strobe** (per generator) — pulse inputs; see
  [Running & clocking](08-running-and-clocking.md).
- **Stage Address** CV (per generator) — selects a stage for strobing and for
  continuous Stage Address mode.
- **External inputs A–D** — four CV inputs usable as the voltage or time source
  for any stage.

## Outputs

- **Voltage Out** (per generator) — the main stage control voltage, updated at
  32 kHz and smoothly interpolated on sloped stages.
- **Time / Reference** — a CV related to the current stage's time, and a ramp
  that sweeps once across each stage.
- **Pulse outputs** — the "all pulses" reference (fires on every stage change)
  plus the two programmable per‑stage pulses (Pulse 1, Pulse 2).

## DIP switches

| Switch | On | Off |
|--------|----|-----|
| **1** | 1.2 V/octave scaling | — |
| **2** | 1 V/octave scaling | — |
| **3** | (unused) | leave **off** always |
| **4** | expander present (32 stages) | no expander (16 stages) |

If neither 1 nor 2 is on, the module uses **2 V/octave**. Changing these affects
voltage scaling and quantizing; recalibrate if you change them.
