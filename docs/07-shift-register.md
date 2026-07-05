# 7. Shift‑register (Turing) mode

Shift‑register mode turns the module's **external voltage source** into a bank of
per‑stage looping shift registers — a "Turing machine" style generator. Instead
of reading an external CV, a stage produces a looping, evolving voltage of its
own, clocked from the external pulse inputs. It is a per‑sequence mode you turn
on and off from the panel.

## What it does

- Each stage has its **own** looping shift register — a repeating sequence of
  **2–16 full‑range voltages** (the loop length is per stage; the value always
  reads the full range). While the sequencer is **on a stage**, each clock
  from its assigned external input plays the **next step of that stage's
  loop**; with odds set by the stage's voltage slider, the bit feeding back
  **slips** and the loop mutates. Stages the sequencer isn't on never move.
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
Full Range → External, twice) to confirm; on leaving, the chase runs in
**reverse** — so every toggle is visibly acknowledged in its direction. While
the mode is on, the **Source (External)** LED of the focused stage **breathes**
(a slow fade) whenever that stage is set to External — a subtle reminder that
its voltage is coming from a register. Hold the chord again to toggle the mode
off.

## Clocking

In this mode the four **external inputs A–D become clocks**. A rising edge on
an input (crossing about **2 V**, re‑arming below ~1 V, so ordinary 5 V clocks
work) clocks the register of the stage the sequencer is **currently on** — if
that stage is assigned to that input (see configuration below). Different
stages can listen to different clocks, or all to one; a stage the sequencer
isn't on never moves.

**Nothing patched? It still works.** The clock is **soft‑normalled to the
sequencer itself**: a stage whose assigned input has no CV present steps its
register **once each time the sequencer enters it**. So with no cables at all,
every visited stage gets one hold‑or‑slip decision per pass — and plugging a
clock into the stage's input takes over (several slips per stage, faster
evolution, or slower from a divided clock).

The stage's **voltage slider sets how much the loop "slips"** per clock — how
likely the recirculating bit is to be replaced with a fresh random one. The
mapping is monotonic: the higher the slider, the more the loop mutates.

| Voltage slider | Behaviour |
|----------------|-----------|
| **Full down** | **Locked** — the same `length` voltages repeat in sequence, exactly. |
| **In between** | **Slip** — the loop mostly repeats, with occasional changes. The slip rate follows a squared curve, so most of the travel is gentle evolution and it ramps up steeply near the top. |
| **Full up** | **Every clock slips** — the sequence never repeats (an ever‑changing stream of full‑range voltages). |

For example: one stage, loop length 4, clock into input A — slider down plays
the same 4 voltages round and round; sliding up mutates that sequence more and
more; at the top it never repeats. (After changing the slider or the length,
the loop takes up to 8 clocks to settle into its exact repeat.)

The slider is read **live** — its physical position is always the slip amount,
even right after loading a program or using a configuration gesture (slider
"pinning" never applies to it).

## Configuring a stage (clock & length)

While the mode is on, hold **Source External** *(without Quantize)* and move a
slider for the **displayed** sequence:

| Gesture | Sets |
|---------|------|
| **Hold Source External + move a *voltage* slider** | which **clock** (input A–D) drives that stage |
| **Hold Source External + move a *time* slider** | that stage's **loop length** (2–16 steps; default 8) |

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
