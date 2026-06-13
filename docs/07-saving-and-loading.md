# 7. Saving & loading programs

The module stores **16 programs**. A program captures the full stage
programming **and** all slider positions, so a recalled program restores your
whole patch. Saving and loading happen **without stopping** the generators —
sequences keep playing throughout.

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

## Notes

- Programs and calibration from **older firmware are not loaded** by this
  version (they are safely ignored). Re‑save your programs after updating.
- The **Clear** switch held down (rather than briefly pressed) clears the
  current program instead of entering save/load — see
  [Front‑panel reference](04-front-panel-reference.md#clear-switch-up--down).
