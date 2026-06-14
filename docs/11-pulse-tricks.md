# 11. Pulse tricks

The generators process Start, Stop and Strobe together, so **simultaneous**
pulses have useful combined meanings.

## Useful combinations

- **Start + Stop together = single advance.** Pulsing Start and Stop at the same
  time advances the generator by exactly one stage without otherwise changing
  its run state. (This is what the panel **Advance** button does.)
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

## Stacking pulses to multiple destinations

If you want to fan one pulse source out to several pulse inputs at once, the
inputs' loading may pull the signal down. A hardware modification to **raise the
input impedance** of the pulse inputs can make stacking reliable. See the
recommended hardware modifications on
[Dave Brown's page](https://modularsynthesis.com/roman/buchla248/248_mods.htm).
