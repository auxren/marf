# 6. Scales & quantizing

When a stage is set to **Quantize**, its voltage is snapped to the nearest note
in the active **scale** for that sequence. By default the scale is **Chromatic**
(every semitone), so quantizing behaves exactly like the classic firmware. You
can also choose a musical scale and root, per sequence, so quantized stages only
ever land on notes that belong together.

## Per‑stage quantize, per‑sequence scale

- **Quantize** is set *per stage* with the **Quantize / Continuous** switch (see
  [Programming a step](05-programming-steps.md#quantize)). Only stages with
  Quantize on are snapped.
- The **scale** and **root** are set *per sequence* (AFG 1 and AFG 2 each have
  their own). Every quantized stage in that sequence uses the sequence's scale.

Quantizing works in semitone space and tracks the volts‑per‑octave
[DIP setting](04-front-panel-reference.md#dip-switches), so a quantized stage
snaps to the nearest in‑scale semitone for your system. On a tie the higher note
is chosen.

## Selecting a scale and root

Hold the **Quantize** switch and use the sliders as selectors for the
**displayed** sequence (choose it first with **Display 1 / Display 2**):

| Gesture | Selects |
|---------|---------|
| **Hold Quantize + move any *voltage* slider** | the **scale** |
| **Hold Quantize + move any *time* slider** | the **root** |

While you hold Quantize:

- The step LEDs show the **number** of whichever you last moved (scale index, or
  root 0–11), so you can read your selection without a screen.
- Slider movement during the gesture **does not change any stage's output** —
  the sliders are frozen as selectors. On release, any slider you moved is
  **pinned** so its stage keeps the value it had before; the slider takes over
  again once you move it back through that value (same behaviour as
  [loading a program](09-saving-and-loading.md#slider-pinning-after-a-load)).
- Do **not** also hold **Source External** — that combination is reserved for
  [shift‑register mode](07-shift-register.md).

The slider sweeps the whole list: at minimum you get **Chromatic / root 0**, and
sweeping up steps through the scales (or the twelve roots) in the order below.

## The scales

| # | Scale | Notes (semitones from root) |
|---|-------|------------------------------|
| 0 | **Chromatic** (default) | all 12 |
| 1 | **Major** | 0 2 4 5 7 9 11 |
| 2 | **Minor** (natural) | 0 2 3 5 7 8 10 |
| 3 | **Harmonic Minor** | 0 2 3 5 7 8 11 |
| 4 | **Dorian** | 0 2 3 5 7 9 10 |
| 5 | **Phrygian** | 0 1 3 5 7 8 10 |
| 6 | **Mixolydian** | 0 2 4 5 7 9 10 |
| 7 | **Lydian** | 0 2 4 6 7 9 11 |
| 8 | **Pentatonic Major** | 0 2 4 7 9 |
| 9 | **Pentatonic Minor** | 0 3 5 7 10 |
| 10 | **Whole Tone** | 0 2 4 6 8 10 |
| 11 | **Octave** | 0 (root octaves only) |

The **root** (0–11) transposes the scale's pattern: root 0 is the unquantized
reference note, root 7 puts the scale a fifth up, and so on.

## Saved with the program

Each sequence's scale and root are stored with the program and restored on load
(see [Saving & loading](09-saving-and-loading.md)). A cleared program resets both
sequences to **Chromatic / root 0**.
