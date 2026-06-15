# MARF 3.0 — Hardware Test Plan

An exhaustive, ID-tagged checklist for verifying the 3.0 firmware on real
hardware. Work through it in order — later tests assume earlier ones pass.
Report failures by ID (e.g. "S3, N7 fail").

Most of this applies to **both** board revisions. The genuinely new **3.0**
behaviour is in sections **M, N, O, P8, A2, B6** — focus there; everything else
is carried over from v2.66 and should already work. Tests that only apply to one
revision are marked. See the [user manual](README.md) for how each feature is
meant to behave.

> **v1 vs v2 pulse wiring** differs (v2: strobe on PB5/PB7, START on PB8/PB6;
> v1: START on PB7/PB5, no strobe jack). On a **v1** board the strobe-input
> tests (I3, I5, I8) do not apply — strobe is panel-only there.

---

## A. Boot & power-up
- [ ] **A1** — Module powers on; both sequences show sane LED state (no lockup, no all-dark/all-lit).
- [ ] **A2** — On a freshly **cleared** program, each stage's max time is **~3 s** (Range 3 default), *not* ~30 s.
- [ ] **A3** — Power-cycle: module returns without a re-flash; saved calibration still in effect.

## B. Calibration (3.0 — two-pass)
- [ ] **B1** — Hold **Stage Address 1 Advance** at power-on → cal mode (**Run/Wait/Stop LEDs cycling**; Display LED **I** = high pass).
- [ ] **B2** — **High pass:** all **knobs and sliders to max**, apply **10 V** to the four external inputs (and the Stage Address jacks if you use external CV — keep those knobs at min).
- [ ] **B3** — *(Optional)* Pulse 1/2 swap with the **OUTPUT PULSE switches**: **Pulse 2 up** = swap, **Pulse 1 up** = normal. One control swaps the **whole chain together** — programming LEDs, switch inputs, and output jacks + their LEDs. The programming-row Pulse LED follows the choice.
- [ ] **B4** — After a swap + **reboot**, the swap persists: the **output pulse jack LEDs** (by the jacks) are swapped to match, not just the programming-row LEDs.
- [ ] **B5** — Press **Stage Address 1 Advance** → captures the high point (Display LED **II** = low pass).
- [ ] **B6** — **Low pass:** all **knobs and sliders to min**, **0 V**/unpatched on the inputs. Press **Stage Address 2 Advance** → saves (erases EEPROM).
- [ ] **B7** — After cal: sliders use their **full travel** (true 0 to full), and inputs/knobs track accurately.
- [ ] **B8** — Pulse 1 switch programs Pulse 1 (and the Pulse 1 jack outputs) if you set the channel swap; LEDs match.
- [ ] **B9** — Power-cycle → calibration persists. **3.0 promise: a reflash of a later 3.0 build must NOT require recal.**

## C. Clear & defaults
- [ ] **C1** — **Hold Clear up** (or down) → program clears, both sequences stop, step LEDs flash.
- [ ] **C2** — After clear: stages default to internal source, continuous, stepped, Range 3, no Stop/Sustain/Enable, no First/Last.
- [ ] **C3** — After clear: both sequences' scale = **Chromatic**, root = **0**.

## D. Display & edit focus
- [ ] **D1** — **Display 1** shows AFG 1; **Display 2** shows AFG 2 (mode LEDs follow the displayed sequence).
- [ ] **D2** — From a view, **Stage No Left/Right** enters **Edit** mode for that sequence at stage 1.
- [ ] **D3** — In edit mode, Stage No Left/Right scrolls the edited stage; **hold** auto-repeats; selection **wraps**.
- [ ] **D4** — In a View mode, programming switches act on the **currently running** stage (reprogram on the fly).

## E. Per-stage programming switches (test on AFG 1 and AFG 2)
- [ ] **E1** — **Voltage range/octave** `0 / 2 / 4 / 6 / 8` each offsets the limited (~1-octave) window to that octave.
- [ ] **E2** — **Full Range** uses the stage's full voltage span (no octave restriction).
- [ ] **E3** — **Pulse 1 On/Off** toggles Pulse 1 output on that stage.
- [ ] **E4** — **Pulse 2 On/Off** toggles Pulse 2 output on that stage.
- [ ] **E5** — **Quantize/Continuous** toggles quantizing on that stage.
- [ ] **E6** — **Sloped/Stepped** toggles glide vs jump on that stage.
- [ ] **E7** — **Source External/Internal** toggles voltage source on that stage.
- [ ] **E8** — **Time Source External/Internal** toggles time source on that stage.
- [ ] **E9** — **Stop / Sustain / Enable** are **mutually exclusive** (setting one clears the others).
- [ ] **E10** — **First / Last**: setting First clears Last on that stage and vice-versa.
- [ ] **E11** — **Time Range 1/2/3/4** selects that stage's time range (audibly different stage lengths).

## F. Sliders & knobs
- [ ] **F1** — Each stage's **voltage slider** sets its output level (internal source).
- [ ] **F2** — Each stage's **time slider** sets its duration within the range.
- [ ] **F3** — **Time Multiply** (per AFG) stretches/compresses all that sequence's stage times together (~×0.5–×4).
- [ ] **F4** — With **Source External**, the **voltage slider position selects which external input** (A–D) feeds the stage.

## G. Voltage output & integration
- [ ] **G1** — **Voltage Out** (each AFG) follows the active stage's level.
- [ ] **G2** — **Stepped** stage: output jumps at stage start.
- [ ] **G3** — **Sloped** stage: output **glides** smoothly previous→current over the stage duration (no stair-stepping; confirms 32 kHz interpolation).
- [ ] **G4** — **Quantize** snaps output to clean semitones; matches the V/oct DIP setting.
- [ ] **G5** — **Reference** output ramps once across each stage.
- [ ] **G6** — **Time** CV output relates to the current stage's time setting.

## H. Pulse outputs
- [ ] **H1** — The **"all pulses" reference** fires on **every** stage change, in all run modes.
- [ ] **H2** — **Pulse 1** fires only on stages with Pulse 1 enabled.
- [ ] **H3** — **Pulse 2** fires only on stages with Pulse 2 enabled.

## I. Running & clocking (pulse inputs + panel)
- [ ] **I1** — **Start** input starts a stopped sequence.
- [ ] **I2** — **Stop** input stops a running sequence.
- [ ] **I3** — **Strobe** input jumps to the **Stage Address**-selected stage (glitch-free S&H). *(v2 only)*
- [ ] **I4** — Panel **Advance** steps exactly one stage (= Start+Stop).
- [ ] **I5** — Panel **Pulse Select** (strobe) jumps to the addressed stage.
- [ ] **I6** — Panel **Reset** returns to stage 1 (no effect in continuous mode — see J2).
- [ ] **I7** — **Start + Stop together** = single advance.
- [ ] **I8** — **Strobe + Start together** = jump to addressed stage *and* run from there. *(v2 only)*

## J. Stage Address & continuous mode
- [ ] **J1** — **Stage Address CV** + Strobe lands on the intended stage across its full range.
- [ ] **J2** — **Continuous Select** engaged: stage follows the Stage Address CV directly (no clock); Reset has no effect; releasing returns to prior run state.

## K. Stop / Sustain / Enable stages
- [ ] **K1** — **Stop stage**: sequence stops on it; a new Start resumes from the next stage.
- [ ] **K2** — **Enable stage**: holds until **Start goes high**, then continues.
- [ ] **K3** — **Sustain stage**: holds while **Start is high**, continues when it goes low (length follows the gate).

## L. Loops (First/Last)
- [ ] **L1** — Running past a **Last** stage jumps back to the nearest preceding **First**.
- [ ] **L2** — With no First set, a Last loops back to **stage 1**.
- [ ] **L3** — **Multiple loops** chained across the stage list each cycle correctly.

## M. Scales & quantizing (3.0) — per displayed sequence
- [ ] **M1** — **Hold Quantize + move a voltage slider** selects the scale (35 scales). The step LEDs show the scale **number in binary** across the 16 LEDs (read in groups of 4 as hex — e.g. scale 17 = LEDs 1 & 5).
- [ ] **M2** — **Hold Quantize + move a time slider** selects the root (0–11); step LEDs show the root as a single lit LED.
- [ ] **M8** — Spot-check a few of the **new scales** (e.g. Dorian, Whole Tone, Raga, Pentatonic, Octave) — quantized notes land in-scale.
- [ ] **M3** — During the hold, moving the slider does **not** change any stage's output (frozen); on release the moved slider is **pinned**.
- [ ] **M4** — Scale/root are **independent per sequence** (set AFG 1 and AFG 2 differently, confirm).
- [ ] **M5** — A **quantized** stage lands only on notes in the selected scale (spot-check Major, Minor, Pentatonic, Whole Tone, Octave).
- [ ] **M6** — Root transposes the scale (e.g. root 7 shifts up a fifth).
- [ ] **M7** — Chromatic scale behaves exactly like classic semitone quantize.

## N. Shift-register / Turing mode (3.0) — per displayed sequence
- [ ] **N1** — **Hold Source External + Quantize ~0.8 s** toggles the mode; entry shows a **mode-LED chase** (Quantize→Sloped→Full Range→External).
- [ ] **N2** — While on, the focused stage's **Source (External) LED breathes** if that stage is External.
- [ ] **N3** — An **Internal** stage has its Source LED off and plays its normal slider voltage (no breathing).
- [ ] **N4** — With the mode on, an **External-source** stage outputs an evolving looping voltage (its register), not the external input.
- [ ] **N5** — The four **external inputs A–D act as clocks**: a rising edge advances the registers assigned to that input.
- [ ] **N6** — The stage's **voltage slider = "big knob"**: full up = **locked loop**; centre = **random**; full down = **double-length/inverted**; between = **slip**.
- [ ] **N7** — **Hold Source External + move a voltage slider** sets which clock input (A–D) drives that stage (step LEDs show 0–3).
- [ ] **N8** — **Hold Source External + move a time slider** sets that stage's loop length (2–16; step LEDs show it).
- [ ] **N9** — The register output still passes through the stage's **range / octave / quantize / scale**.
- [ ] **N10** — Toggling the mode **off** restores normal external-input behaviour.

## N2. Randomize (3.0)
- [ ] **N2.1** — Select an AFG (**Display 1** or **Display 2**), then hold **both Stage Address Reset buttons (AFG 1 + AFG 2)** for >1 s → a ~2 s twinkling LED show plays.
- [ ] **N2.2** — Afterward the displayed AFG's stages are randomized: every stage has random slider levels, voltage range/octave, quantize/slope/pulse states, pulse width, and time range (never the slow 30 s range).
- [ ] **N2.3** — The block has a random loop length (First on its first stage, Last on a random stage) and **plays immediately** (sources stayed Internal, no Stop/Sustain/Enable).
- [ ] **N2.4** — Sliders are **pinned** to the random values until moved through them. The displayed AFG resets to its first stage.
- [ ] **N2.5** — **Only the displayed AFG is affected**: randomize AFG 1, switch to Display 2, confirm AFG 2 is unchanged; then randomize AFG 2 and confirm AFG 1 is still as it was. Each AFG randomizes independently (AFG 1 = stages 1–16, AFG 2 = stages 17–32).

## N3. Per-step pulse width (3.0)
- [ ] **N3.1** — Hold the chord **Time Source up (External) + Time Range 1 (.03) up** and move a step's **time slider** → step LEDs show a **bar**; that step's pulse outputs (Pulse 1/2 and ALL) get wider/narrower (gate ~1%–99% of the step).
- [ ] **N3.2** — The pulse always returns low within the step (so it can re-trigger), even at max width.
- [ ] **N3.3** — Moving the slider during the gesture does **not** change the step's *time*; the moved slider is pinned on release.
- [ ] **N3.4** — Pulse width is **saved/loaded** with the program (P-section) and **randomized** by the randomize chord (N2).
- [ ] **N3.5** — On release, the step's **time source and time range return to what they were** before the chord. Setting widths across several stages **while running** does **not** stamp other steps' time range, and the LED bar tracks the slider you're moving.

## O. Soft-normalled external inputs (3.0)
- [ ] **O1** — External-source stage **with** a CV patched to its chosen input → uses the external CV.
- [ ] **O2** — Same stage with **nothing patched** → falls back to its own slider value (no dead/zero output).

## P. Saving & loading (3.0 storage)
- [ ] **P1** — **Clear down (brief)** enters Save mode (Pulse LEDs toggle).
- [ ] **P2** — Select a slot with **Stage No Left/Right** (step LEDs show it); **Clear down again** saves; step LEDs **flash downward**.
- [ ] **P3** — **Clear up (brief)** enters Load mode; select slot; **Clear up again** loads; step LEDs **flash upward**.
- [ ] **P4** — Pressing a **Display** button **aborts** save/load.
- [ ] **P5** — All **16 slots** are independently usable.
- [ ] **P6** — Loading an **empty/never-saved** slot → quick **triple flash**, nothing loaded (current patch untouched).
- [ ] **P7** — After a load, sliders are **pinned** to stored values until moved through them.
- [ ] **P8** — A recalled program restores **scale/root**, each AFG's **stage shift (section)** and each stage's **shift-register clock/length**.
- [ ] **P9** — Save and load happen **without stopping** the running sequences.
- [ ] **P10** — **Factory presets**: on a fresh chip, every never-saved slot loads a playable sequence (I Feel Love, Berlin School, Halloween, Stranger Things, Bach, Reich phasing, polymeter, acid, On the Run, TEE, Cars, Blue Monday, Blade Runner, Glass, drift, harmony). Each plays immediately with nothing patched. Every preset is **two-part**: AFG 1 on stages 1–16, AFG 2 on 17–32.
- [ ] **P10b** — **Saved stage shift**: loading a preset restores **AFG 2 to stages 17–32** and AFG 1 to 1–16 (the shift is saved in the program) — starting both AFGs plays the full arrangement with no manual shift. Saving a patch with AFG 2 shifted and reloading restores that shift. (16-slider board; expander leaves sections alone.)
- [ ] **P11** — A factory slot you **save over** keeps your version permanently (it's now user-owned); it is **not** re-seeded by a bank update. A slot you **clear** is re-seeded at the next boot. A firmware update with a new bank refreshes the factory-owned slots only, leaving user saves intact.

## Q. Section shift (unexpanded only — DIP 4 off)
- [ ] **Q1** — **Display 1 + Stage No Right** moves AFG 1 to stages **17–32**.
- [ ] **Q2** — **Display 1 + Stage No Left** moves AFG 1 back to **1–16**.
- [ ] **Q3** — **Display 2 + Stage No Right / Left** does the same for AFG 2.
- [ ] **Q4** — In the 17–32 bank, switch programming is independent but slider *levels* are still the physical 1–16 sliders.
- [ ] **Q5** — **Shift indicator**: the viewed generator's **Display LED is steady** on stages 1–16 and **blinks (~2 Hz)** when shifted to 17–32. Switching Display 1/2 reads each generator; loading a two-part preset shows AFG 2's Display LED blinking.

## R. Expander (DIP 4 on, expander attached)
- [ ] **R1** — Stage count is a full **32** with their own sliders read directly.
- [ ] **R2** — Section-shift combos (Q) have **no shifting effect** with the expander present.

## S. DIP switches
- [ ] **S1** — Switch **1 on** → 1.2 V/octave scaling.
- [ ] **S2** — Switch **2 on** → 1 V/octave scaling.
- [ ] **S3** — Neither 1 nor 2 → 2 V/octave.
- [ ] **S4** — Switch **4** toggles expander / 32-stage mode (matches R1).
- [ ] **S5** — Switch **3** has no effect (leave off).

## T. LED signals & fault handling
- [ ] **T1** — Save = step LEDs flash **down**; Load = flash **up**; refused load = **triple flash**.
- [ ] **T2** — Save/Load mode = **Pulse LEDs toggled**.
- [ ] **T3** — Cal mode = **Run/Wait/Stop LEDs cycling**.
- [ ] **T4** — Turing-on = **mode-LED chase**; Turing-active = **Source LED breathe**.
- [ ] **T5** *(optional/hard)* — On a caught firmware fault, **Display LEDs blink** and the watchdog auto-restarts (~3 s). Don't force it; just note if seen.

---

### Reminders
- For **N (Turing)** the voltage slider has two roles: held *with* Source External it's the clock-assignment selector (N7); on its own with the mode on it's the "big knob" (N6).
- If **B6** ever fails (a 3.0→3.0 reflash demands recal), that's a regression against the frozen-calibration guarantee — flag it.
