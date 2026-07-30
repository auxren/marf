# 11. Pulse tricks

The generators process Start, Stop and Strobe together, so **simultaneous**
pulses have useful combined meanings.

## Useful combinations

- **Start + Stop together = single advance.** Pulsing Start and Stop at the same
  time advances the generator by exactly one stage without otherwise changing
  its run state. (This is what the panel **Advance** button does.) On v2, a
  **steady stream** of such pulses locks the generator to that clock — the Time
  Multiply knob then selects a clock ratio and the time sliders become a
  per‑stage shuffle; see
  [External clock sync](08-running-and-clocking.md#external-clock-sync-v2).
- **Strobe + Start = start on the addressed stage.** Pulsing Strobe and Start
  together jumps to the stage selected by the **Stage Address** CV *and* starts
  running from there — handy for launching a sequence at a chosen entry point.

## Driving Sustain / Enable stages

The **Start** input also releases holds:

- On an **Enable** stage, the generator waits until **Start goes high**.
- On a **Sustain** stage, the generator holds while **Start is high** and
  continues when it goes **low** — so a gate on Start makes that stage's length
  follow the gate.

Combine these with programmed stages to build sequences that wait for, or are
shaped by, external events.

## Patch idea: per‑stage clock ratios (self‑patch)

*Needs 3.3.1's sliders‑as‑voltages mode.*

With the generator locked to an external clock and **humanize toggled off**
(Time Source External + "30" Time Range chord — see
[Sliders‑as‑voltages mode](08-running-and-clocking.md)), the time sliders are
a free voltage row on the **Time OUT**. Now self‑patch:

> **Time OUT → Time Multiply CV input** (same generator), with the Time
> Multiply knob parked fully counter‑clockwise.

The Time Multiply CV sums with the knob and the clock‑ratio zones re‑read
continuously — so **each stage's time slider now selects that stage's own
clock ratio**. Stage 1 in the ×4 zone plays sixteenths, stage 2 in ×2 plays
eighths, stage 3 in ÷8 stretches across eight clock pulses: a per‑stage
polymetric ratchet sequencer from one patch cable.

Behaviour worth knowing (verified against the firmware):

- **Divide stages are exact** — a ÷N stage lasts exactly N clock pulses, and
  **×1 stages** land exactly on the clock.
- **Multiply stages subdivide the clock period they start in.** Runs of the
  same multiply ratio are clean (four ×4 stages = perfect sixteenths); a
  multiply stage entered mid‑cycle fills the *remainder* of that cycle —
  deterministic, and part of the charm.
- There are **15 zones (÷8 … ×8)** across the CV range with hysteresis, so a
  dialed‑in slider stays put. Enabling **Quantize** on those stages snaps the
  sliders to reproducible voltages, which makes zone‑dialing repeatable —
  and the ratio pattern recallable with saved programs.
- The knob is the **base** the CV adds to: parking it CCW gives the sliders
  the whole zone range; parking it at "1" lets a small CV push ratios up
  from unity instead.

## Stacking pulses to multiple destinations

If you want to fan one pulse source out to several pulse inputs at once, the
inputs' loading may pull the signal down. A hardware modification to **raise the
input impedance** of the pulse inputs can make stacking reliable. See the
recommended hardware modifications on
[Dave Brown's page](https://modularsynthesis.com/roman/buchla248/248_mods.htm).
