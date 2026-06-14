# 9. Saving & loading programs

The module stores **16 programs**. A program captures the full stage
programming **and** all slider positions — including each sequence's
[scale and root](06-scales.md), each AFG's **stage shift** (whether it plays
stages 1–16 or 17–32) and every stage's
[shift‑register clock and length](07-shift-register.md) — so a recalled program
restores your whole patch. Saving and loading happen **without stopping** the
generators — sequences keep playing throughout.

Each stored program is written with a checksum and a format version, so the
module can tell a real program from an empty, corrupt or older‑format slot and
will refuse to load garbage (see below).

## Save a program

1. **Briefly press Clear Down** to enter **Save** mode. The Pulse LEDs toggle to
   show you are in save mode.
2. Use **Stage No Left / Right** to choose one of the 16 memory locations. The step
   LEDs show the selected location.
3. **Briefly press Clear Down again** to save.
4. The step LEDs **flash downward** to confirm the save.

To **abort** without saving, press either **Display** button.

## Load a program

1. **Briefly press Clear Up** to enter **Load** mode. The Pulse LEDs toggle to
   show you are in load mode.
2. Use **Stage No Left / Right** to choose a memory location.
3. **Briefly press Clear Up again** to load.
4. The step LEDs **flash upward** to confirm the load.

To **abort**, press either **Display** button.

### If a slot is empty or unreadable

If the selected slot holds no valid program — it was never saved, or it was
written by older firmware, or the data is corrupt — the step LEDs give a quick
**triple flash** and **nothing is loaded**: the program you were playing is left
untouched. Pick another slot or save one first.

## Slider pinning after a load

When a program loads, every slider is **pinned** to its stored value — the
physical slider position is ignored until you move it. To "un‑stick" a slider,
**move it through its stored value**; from then on it tracks normally. Sweeping a
stuck slider from minimum to maximum always frees it.

This lets you recall a precise patch even though the physical sliders are
wherever you last left them, and hand control back gradually as you touch each
one.

## Factory presets

A fresh module (or any slot you've never saved to) comes **pre‑loaded** with a
bank of ready‑to‑play sequences — load them like any other program. They are a
starting point: audition one, tweak it, and **save over the slot** to keep your
version. The factory bank only ever fills slots that are empty or unreadable, so
it **never overwrites a program you saved**.

The bank is a mix of recognizable synth lines and patterns that show off the
module (slides, odd loop lengths, the two independent AFGs):

| Slot | Sequence | Shows off |
|------|----------|-----------|
| **1** | Moroder / Donna Summer, *"I Feel Love"* | octave-bouncing bass, fast + gated |
| **2** | Tangerine Dream / Berlin School | driving minor arpeggio with sloped accents |
| **3** | John Carpenter, *"Halloween"* | 5/4 ostinato — a **5-step loop** (odd meter) |
| **4** | *Stranger Things* (S U R V I V E) | bright ascending arpeggio |
| **5** | Bach, *Prelude in C* | quantized major arpeggio (Switched-On nod) |
| **6** | Steve Reich **phasing** | identical cell on both AFGs — nudge one **Time Multiply** and they drift in/out of phase |
| **7** | **Polymeter** | AFG 1 (7 steps) against AFG 2 (5 steps) — never re-aligns |
| **8** | **Acid / 303** | minor-pentatonic bass with **Sloped stages as slides** + accents |
| **9** | Pink Floyd, *"On the Run"* | the actual EMS Synthi 8-note sequence at speed, stepped + gated |
| **10** | Kraftwerk, *"Trans-Europe Express"* | motoric repetitive bass |
| **11** | Gary Numan, *"Cars"* | syncopated minor riff |
| **12** | New Order, *"Blue Monday"* | fast driving 16th bass |
| **13** | Vangelis, *"Blade Runner"* | lush slow pad, sloped, long gates |
| **14** | Philip Glass-style minimalism | hypnotic shifting arpeggio |
| **15** | **Generative drift** | a long 13-step whole-tone loop that never settles |
| **16** | **Two-voice harmony** | AFG 1 bass + AFG 2 a voice above, same length |

Each preset sets a fitting [scale and root](06-scales.md) and quantizes every
stage, so the figures stay in key even after you nudge a slider. They use
internal sources only, so they play immediately with nothing patched.

**Every preset is a two‑part arrangement.** AFG 1 plays stages **1–16** and AFG 2
plays stages **17–32**, and the two parts are written to complement each other —
e.g. a bass under an arpeggio, a pad under a riff, a counter‑melody, or (for
*Reich* and *Polymeter*) the same/related cell at a different length. Each AFG's
**stage shift is saved with the program**, so **loading a preset restores AFG 2
to stages 17–32** (and AFG 1 to 1–16) automatically — just **start both
generators** to hear the whole thing, no manual shift needed. (Your own saves
restore whatever shift each AFG had when you saved.) Running AFG 1 alone still
gives you a complete, musical sequence. (Stage shifts apply on a 16‑slider board;
with the expander all 32 stages share one section.)

The module remembers which slots it filled with factory presets. The instant
you **save your own program** over a slot, that slot becomes yours and is **never
touched again**. The factory slots you *haven't* claimed are automatically
refreshed whenever a firmware update ships a new or improved preset bank — so
updating brings in the new sequences without you clearing anything, and without
disturbing the programs you saved.

## Notes

- Programs and calibration from **older firmware are not loaded** by this
  version (they are safely ignored). Re‑save your programs after updating.
- The **Clear** switch held down (rather than briefly pressed) clears the
  current program instead of entering save/load — see
  [Front‑panel reference](04-front-panel-reference.md#clear-switch-up--down).
