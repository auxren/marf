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

- The step LEDs show the **number** of whichever you last moved. The **root**
  (0–11) is shown as a single lit LED. The **scale number** (1–35) is shown in
  **binary across the 16 step LEDs** — read the LEDs in groups of four as **hex**
  digits (e.g. scale 17 lights LEDs 1 and 5 = `0x11`; scale 35 = `0x23`). This is
  how more than 16 scales fit on 16 LEDs.
- Slider movement during the gesture **does not change any stage's output** —
  the sliders are frozen as selectors. On release, any slider you moved is
  **pinned** so its stage keeps the value it had before; the slider takes over
  again once you move it back through that value (same behaviour as
  [loading a program](09-saving-and-loading.md#slider-pinning-after-a-load)).
- Do **not** also hold **Source External** — that combination is reserved for
  [shift‑register mode](07-shift-register.md).

The slider sweeps the whole list: at minimum you get **Chromatic / root 0**, and
sweeping up steps through the 35 scales (or the twelve roots) in the order below.

## The scales

| # | Scale | Intervals |
|---|-------|-----------|
| 1 | **Chromatic** (default) | 1 2b 2 3b 3 4 5b 5 6b 6 7b 7 |
| 2 | **Ionian** (Major) | 1 2 3 4 5 6 7 |
| 3 | **Dorian** | 1 2 3b 4 5 6 7b |
| 4 | **Phrygian** | 1 2b 3b 4 5 6b 7b |
| 5 | **Lydian** | 1 2 3 4# 5 6 7 |
| 6 | **Mixolydian** | 1 2 3 4 5 6 7b |
| 7 | **Aeolian** (Minor) | 1 2 3b 4 5 6b 7b |
| 8 | **Locrian** | 1 2b 3b 4 5b 6b 7b |
| 9 | **Harmonic Minor** | 1 2 3b 4 5 6b 7 |
| 10 | **Melodic Minor** | 1 2 3b 4 5 6 7 |
| 11 | **Major Blues** | 1 2 3b 3 5 6 |
| 12 | **Minor Blues** | 1 3b 4 5b 5 7b |
| 13 | **Diminished** | 1 2 3b 4 4# 5# 6 7 |
| 14 | **Combination Diminished** | 1 2b 3b 3 4# 5 6 7b |
| 15 | **Major Pentatonic** | 1 2 3 5 6 |
| 16 | **Minor Pentatonic** | 1 3b 4 5 7b |
| 17 | **Raga Bhairav** | 1 2b 3 4 5 6b 7 |
| 18 | **Raga Gamanasrama** | 1 2b 3 4# 5 6 7 |
| 19 | **Raga Todi** | 1 2b 3b 4# 5 6b 7 |
| 20 | **Arabian** | 1 2 3 4 5b 6b 7b |
| 21 | **Spanish** | 1 2b 3b 3 4 5 6b 7b |
| 22 | **Gypsy** | 1 2 3b 4# 5 6b 7 |
| 23 | **Egyptian** | 1 2 4 5 7b |
| 24 | **Hawaiian** | 1 2 3b 5 6 |
| 25 | **Balinese Pelog** | 1 2b 3b 5 6b |
| 26 | **Japanese Miyakobushi** | 1 2b 4 5 6b |
| 27 | **Ryuku** | 1 3 4 5 7 |
| 28 | **Chinese** | 1 2 4# 5 7 |
| 29 | **Bass Line** | 1 5 7b |
| 30 | **Whole Tone** | 1 2 3 5b 6b 7b |
| 31 | **Minor 3rd Interval** | 1 3b 5b 6 |
| 32 | **Major 3rd Interval** | 1 3 6b |
| 33 | **4th Interval** | 1 4 7b |
| 34 | **5th Interval** | 1 5 |
| 35 | **Octave** | 1 (root octaves only) |

The **root** (0–11) transposes the scale's pattern: root 0 is the unquantized
reference note, root 7 puts the scale a fifth up, and so on.

## Saved with the program

Each sequence's scale and root are stored with the program and restored on load
(see [Saving & loading](09-saving-and-loading.md)). A cleared program resets both
sequences to **Chromatic / root 0**.
