# 7. Shift‑register (Turing) mode

Shift‑register mode turns the module's **external voltage source** into a bank of
per‑stage looping shift registers — a "Turing machine" style generator. Instead
of reading an external CV, a stage produces a looping, evolving voltage of its
own, clocked from the external pulse inputs. It is a per‑sequence mode you turn
on and off from the panel.

## What it does

- Each stage has its **own** shift register (a loop of 2–16 bits). On each clock
  the bit that wraps around is fed back — copied, inverted or randomised — and
  the register is read out as a voltage.
- A register only drives a stage that is set to **Source External**. With the
  mode on, such a stage uses its register instead of an external input; the
  output still passes through the stage's **range / octave / quantize / scale**
  exactly like a real external CV, so you can quantize the pattern to a
  [scale](06-scales.md).
- A stage set to **Source Internal** is untouched — it plays its slider voltage
  as usual.

## Turning it on / off

Hold **Source External + Quantize together for about 0.8 s** to toggle the mode
for the **displayed** sequence (pick it first with **Display 1 / Display 2**).

On entering, the mode LEDs play a short **chase animation** (Quantize → Sloped →
Full Range → External, twice) to confirm. While the mode is on, the **Source
(External)** LED of the focused stage **breathes** (a slow fade) whenever that
stage is set to External — a subtle reminder that its voltage is coming from a
register. Hold the chord again to toggle the mode off.

## Clocking

In this mode the four **external inputs A–D become clocks**. A rising edge on an
input (the CV crossing mid‑scale) clocks every stage assigned to that input.
Each stage is assigned to one of the four clocks (see configuration below), so
you can run different stages from different clocks, or all from one.

The stage's **voltage slider acts as the "big knob"** that sets how the loop
evolves on each clock:

| Voltage slider | Behaviour |
|----------------|-----------|
| **Full up** | **Locked** — the loop repeats exactly (period = length). |
| **Centre** | **Random** — the pattern never repeats. |
| **Full down** | **Inverted / double‑locked** — a longer, mirrored loop (period = 2× length). |
| **In between** | **Slip** — mostly looping, with occasional changes. |

## Configuring a stage (clock & length)

While the mode is on, hold **Source External** *(without Quantize)* and move a
slider for the **displayed** sequence:

| Gesture | Sets |
|---------|------|
| **Hold Source External + move a *voltage* slider** | which **clock** (input A–D) drives that stage |
| **Hold Source External + move a *time* slider** | that stage's **loop length** (2–16) |

As with [scale selection](06-scales.md#selecting-a-scale-and-root), the step LEDs
show the value, the sliders are frozen during the gesture so they don't change
the running pattern, and any slider you move is pinned on release.

> Remember the voltage slider has two jobs in this mode: held *with* Source
> External it sets the clock assignment; on its own (mode on, nothing held) it is
> the "big knob" for that stage's register.

## Saved with the program

Each stage's **clock assignment** and **loop length** are stored with the program
and restored on load. The live register *contents* are not saved — they re‑seed
randomly at power‑up — so a recalled program reproduces the configuration, and
the patterns regrow from there.
