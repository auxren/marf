# 1. Overview

The **Multiple Arbitrary Function Generator (MARF, Buchla 248r)** is a complex
control‑voltage sequencer in the Buchla 200‑series tradition. Rather than only
stepping through pitches, each *stage* produces an arbitrary control voltage and
a timing value, and the module marches through the stages as a programmable
function generator.

## Two function generators

The module contains **two independent arbitrary function generators**, referred
to throughout as **AFG 1** and **AFG 2** (the panel labels them sequence/section
I and II). Each one:

- walks through a series of **stages** (steps), producing a **Voltage Out** and
  other per‑stage signals;
- has its own **Start**, **Stop** and **Strobe** pulse inputs, a **Stage
  Address** CV input and a **Time Multiply** control;
- can run, stop, be advanced manually, jump (strobe) to an addressed stage, or
  follow its Stage Address CV continuously.

Both generators normally share the **same stage programming** (the same 16 — or
32 with an expander — stage definitions). This matches the original 200‑series
module. See [Section shift](10-section-shift.md) for how to point the two
generators at different blocks of stages on an un‑expanded unit.

## What each stage stores

Every stage holds, independently:

- a **Voltage** level (from its slider, from an external input, or from its own
  [shift‑register generator](07-shift-register.md)), with options for full or
  limited (octave) range and [scale‑aware quantizing](06-scales.md);
- a **Time** (duration), from its slider or an external input, across four
  ranges and scaled by the Time Multiply control;
- two programmable **Pulse** outputs (Pulse 1 / Pulse 2);
- an **integration** choice — *sloped* (glide to this stage's level) or
  *stepped* (jump);
- an operating **mode** — normal, **Stop**, **Sustain** or **Enable**;
- **loop** markers — *First* and *Last* — for cycling.

See [Programming a step](05-programming-steps.md) for the details.

## Outputs

For each generator the module provides:

- **Voltage Out** — the main control voltage for the current stage, updated at
  a high sample rate and smoothly interpolated when a stage is *sloped*;
- a **Time** CV related to the current stage's time setting;
- a **Reference** ramp that sweeps once across each stage;
- **Pulse** outputs — an "all pulses" reference that fires on every stage change,
  plus the two programmable per‑stage pulses.

## Expander

With a compatible expander connected (and the DIP switch set), the stage count
doubles from **16 to 32**, and the extra sliders are read directly. Without an
expander the module has 16 stages, with a [section‑shift](10-section-shift.md)
trick to reach a second bank of 16.

## Firmware lineage

This firmware was extensively rewritten (v2.66) to closely match the original
200‑series module and to fix many timing and voltage glitches: programmed pulses
and the reference signal fire on every stage change in all modes, sloped stages
slew correctly, quantizing and loop behaviour are correct, the output sample
rate is 32 kHz, and step times match the panel legend.

The later **3.0‑line** work adds reliability fixes (accurate timing delays, a
watchdog that restarts on a caught fault) and checksummed memory, plus three
new musical features, all driven from the existing panel:

- per‑sequence **[scales and roots](06-scales.md)** for the quantizer;
- a per‑stage **[shift‑register (Turing) mode](07-shift-register.md)** that turns
  the external voltage source into looping, evolving CV generators;
- **soft‑normalled** external inputs — an external‑source stage uses its external
  input when a CV is patched and falls back to its own slider when nothing is.

These additions build from one source tree for both the SAModular/EMS **v2**
board and the original **v1.6** board (see
[Installation](02-installation-and-flashing.md#hardware-revisions-v2-vs-v16)).
See `RELEASE_NOTES.txt`.
