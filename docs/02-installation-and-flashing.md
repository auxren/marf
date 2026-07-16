# 2. Installation & flashing

The firmware runs on the module's STM32F405 microcontroller and is flashed over
the SWD header with an ST‑Link programmer.

## What you need

- An **ST‑Link v2** STM32 programmer (inexpensive and widely available).
- **STM32CubeProgrammer** (free from st.com, available for macOS, Windows and
  Linux).
- A firmware image — either a released `.hex`
  ([releases page](https://github.com/auxren/marf/releases)) or one you build
  yourself (below).

## Flashing procedure

1. **Power down** the case.
2. Remove the module from the case, leaving the power cable connected, and
   attach the ST‑Link ribbon cable to the module's programming header.
3. **Power on** the case.
4. In STM32CubeProgrammer, connect to the target via ST‑Link and **program** the
   firmware image to flash (address `0x08000000`).
5. Power off, disconnect the ST‑Link, and reseat the module in the case.
6. Power back on.
7. **Calibrate if needed.** Calibration lives in the external EEPROM, so
   flashing does not erase it. Coming from **older (pre‑3.0) firmware** the
   stored calibration is in an incompatible format and you *must* recalibrate —
   see [Calibration](03-calibration.md). Updating between **3.0+** builds keeps
   your calibration, so this step is then optional.

> **Memory note:** this firmware stores programs and calibration in a
> checksummed, versioned format. Data saved by **older (pre‑3.0)** firmware is
> not read back (it is safely ignored), so the first time you move onto 3.0,
> plan to recalibrate and re‑save your programs. From **3.0 onward the
> calibration format is frozen** — later updates read it back and need no
> recalibration. (Saved programs may still need re‑saving if the *program*
> format is revised; calibration is independent.)

## Building from source

The repository builds with the Arm GNU bare‑metal toolchain (`arm-none-eabi`).

```sh
make            # -> build/MARF.elf, build/MARF.hex, build/MARF.bin
make clean
make test       # run the host unit tests (no hardware required)
```

If the toolchain is not on your `PATH`, point the build at it:

```sh
make TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain/bin/
```

You can also import the project into STM32CubeIDE (`.cproject` / `.project` are
included) and build there. Every push and pull request is built automatically by
CI, and tagged releases publish `.hex` and `.bin` images.

## Hardware revisions (v2 vs v1.6)

This firmware targets two board revisions, selected at build time by the
`MARF_HW` make variable (default `2`). The differences are confined to GPIO
pin assignments — DIP-switch pins and pulse-input wiring — and are described in
`src/marf_version.h`.

- **v2 (default)** — the SAModular / EMS "v2" board. This is what `make`
  builds, and what the main `*.hex` release asset targets.
- **REV1** — the original v1.x board (B248 rev 1.0). Built with `make rev1`
  into `build-rev1/`, and published on releases as `*-REV1.hex`. v1 pulse
  jacks: STOP 1/2 on PB0/PB1, START 1/2 on PB7/PB5 (inverted input
  conditioning), STROBE 1/2 on PB2/PB14. Validated on real v1 hardware as of
  v3.2.1.

> **Do not flash the REV1 image to a v2 board** (or vice versa) — the DIP,
> pulse and analog-mux handling differ. If unsure which board you have, check
> the pulse-input wiring or ask before flashing.

## DIP switches

Before using the module, set the configuration DIP switches for your hardware —
volts‑per‑octave scaling and whether an expander is present. See
[DIP switches](04-front-panel-reference.md#dip-switches).
