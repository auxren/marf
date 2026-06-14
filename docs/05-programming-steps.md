# 5. Programming a step

Each stage is defined independently. Focus a stage (see
[Display & edit focus](04-front-panel-reference.md#display--edit-focus)), then
set its voltage, time, pulses and behaviour with the programming switches and
sliders.

## Voltage

The stage's **Voltage Out** comes from either the stage's **voltage slider** or
an **external input**:

- **Source Internal** — the slider sets the level.
- **Source External** — the level is read from one of the four external inputs
  (**A–D**). With external source selected, the **voltage slider position
  chooses which input** is used. External‑source stages are **soft‑normalled**:
  if nothing is patched to the chosen input, the stage falls back to its own
  slider value. With [shift‑register mode](07-shift-register.md) on, an
  external‑source stage instead reads its own looping register.

### Range and octave

- **Full Range** — the slider spans the stage's full voltage range.
- **Limited range (`0` / `2` / `4` / `6` / `8`)** — the slider spans a smaller,
  roughly one‑octave window, offset to sit around the chosen octave. This makes
  it easy to dial precise pitches within a known octave. The number is the
  octave offset; `0` is no offset.

### Quantize

- **Quantize** snaps the stage output to the nearest note in the sequence's
  active **[scale](06-scales.md)**, tracking the volts‑per‑octave set by the DIP
  switches. By default the scale is **Chromatic** (every semitone, 12‑tone equal
  temperament).
- **Continuous** leaves the voltage unquantized (smooth).

Quantizing applies after range/octave scaling, so a quantized limited‑range
stage gives clean in‑scale steps within its octave. To pick a musical scale and
root for the sequence, see **[Scales & quantizing](06-scales.md)**.

### Integration: sloped vs stepped

- **Stepped** — the output jumps to this stage's level at the start of the
  stage.
- **Sloped** — the output **glides** from the previous stage's level to this
  stage's level over the stage's duration. The glide is interpolated at the full
  32 kHz output rate, so slopes are smooth even on long stages.

## Time

Each stage's **duration** comes from its **time slider** (or an external input
if **Time Source External** is selected) within one of four **time ranges**,
then scaled by the generator's **Time Multiply** control.

| Time Range | Approx. full‑scale stage time (×1 multiplier) |
|------------|-----------------------------------------------|
| 4 | ~2–30 s |
| 3 | ~0.2–3 s *(default on a cleared program)* |
| 2 | ~0.02–0.3 s |
| 1 | ~0.002–0.03 s |

A freshly **cleared** program starts every stage in **Range 3** (a musically
useful ~3 s maximum) rather than the slow 30 s range.

The **Time Multiply** knob scales all of that generator's stage times together
(roughly ×0.5 to ×4), so you can stretch or compress a whole sequence without
touching individual sliders. At the maximum time range and multiplier, a single
stage can last around two minutes.

## Pulses

Each stage can emit **Pulse 1** and/or **Pulse 2** when it becomes active:

- **Pulse 1 On / Off**, **Pulse 2 On / Off** enable each pulse for the stage.
- A short "all pulses" reference also fires on **every** stage change,
  regardless of the per‑stage pulse settings.

Use the programmed pulses to trigger envelopes, drums or other events in sync
with specific stages.

### Per‑step pulse width (gate length)

Each stage's pulse outputs (Pulse 1, Pulse 2 **and** the "all pulses" reference)
have a programmable **width** — from a short trigger (~1 % of the step) up to a
near‑full gate (~99 %, always dropping low again so it can re‑trigger). Set it by
**holding Time Source up (External) + the .03 Time Range switch up, and moving
that step's time slider**; the step LEDs show the width as a bar. (The held
chord's normal effect on the step's time source / time range is restored when
you release.) The width is saved with the program and is included when you
[randomize](04-front-panel-reference.md#randomize).

## Operating mode (Stop / Sustain / Enable)

A stage can carry one special operating mode (mutually exclusive — setting one
clears the others). With all of them **off**, the stage is a normal timed stage.

- **Stop** — when the generator reaches this stage it **stops** there.
- **Enable** — the generator **holds** on this stage until the **Start** input
  goes high, then continues.
- **Sustain** — the generator **holds** on this stage while **Start** is high
  and continues when it goes low.

See [Running & clocking](08-running-and-clocking.md) for exactly how these
interact with the pulse inputs.

## Loops (First / Last)

- **First** marks a stage as a loop start.
- **Last** marks a stage as a loop end. When the generator finishes a *Last*
  stage it jumps back to the nearest preceding *First* (or to stage 1 if there
  is none). Multiple loops are supported.

Setting **First** clears **Last** on that stage and vice‑versa.
