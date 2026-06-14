# 8. Running & clocking

Each generator advances through its stages under the control of its **Start**,
**Stop** and **Strobe** pulse inputs and its **Stage Address** CV, plus the
panel's Stage Address controls.

## Run states

A generator is always in one of these states:

- **Stopped** — not advancing. Holds the current stage's output.
- **Running** — advancing stage to stage on each stage's timer.
- **Continuous Stage Address** — the stage follows the Stage Address CV directly
  rather than a clock (see below).
- **Holding (Enable / Sustain)** — paused on an Enable or Sustain stage waiting
  for the Start input.

## Pulse inputs

| Input | Effect |
|-------|--------|
| **Start** | Starts a stopped generator running. On an Enable/Sustain stage, releases the hold (see below). |
| **Stop** | Stops a running generator. |
| **Strobe** | Immediately jumps to the stage selected by the **Stage Address** CV. |

Simultaneous combinations are meaningful — see [Pulse tricks](11-pulse-tricks.md).
The panel **Advance** button is equivalent to a simultaneous Start + Stop
(advance one stage); the panel **Pulse Select** button is equivalent to a Strobe.

## Stop / Sustain / Enable stages

These per‑stage modes (set when [programming a step](05-programming-steps.md#operating-mode-stop--sustain--enable))
change what happens when the generator reaches the stage:

- **Stop stage** — the generator stops on this stage. A new **Start** pulse
  resumes from the next stage.
- **Enable stage** — the generator holds here. It continues when **Start** goes
  high. (If Start is already high when the stage is reached, it continues.)
- **Sustain stage** — the generator holds here while **Start** is high and
  continues once Start goes low. This makes a stage's length follow a gate.

## Continuous Stage Address mode

Engage **Continuous Select** (panel) for a generator and its current stage
**follows the Stage Address CV** continuously: sweeping the CV sweeps through the
stages, and the generator does not run on its own timer. Release to return to
the previous run state. **Reset** has no effect in this mode.

This turns a generator into a voltage‑addressed lookup of your programmed stages
— useful for scanning or for externally sequencing the stage order.

## Strobe / addressed jumps

A **Strobe** pulse (or the panel **Pulse Select**) jumps immediately to the
stage chosen by the **Stage Address** CV. The output is sample‑and‑held across
the jump so there is no glitch, and freshly‑read CV is used so the jump lands on
the intended stage.

## Loops

With **First** and **Last** loop markers programmed
([see step programming](05-programming-steps.md#loops-first--last)), a running
generator cycles: on finishing a *Last* stage it returns to the nearest
preceding *First* stage (or stage 1 if none is set). Several loops can be chained
across the stage list.

## Manual control

- **Advance** steps one stage at a time (handy for programming and auditioning).
- **Reset** returns to stage 1.
- **Display 1 / 2** choose which generator the panel currently shows and edits.
