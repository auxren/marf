# Multiple Arbitrary Function Generator

This repo contains source code for the Buchla 248r module, originally produced 
by the Electric Music Store and subsequently improved by SA Modular.

## Documentation

A full user manual lives in [`docs/`](docs/README.md). A combined **PDF**
(`*-manual.pdf`) is attached to each [release](https://github.com/auxren/marf/releases),
or build it locally with `make manual`.

## Installation

1.  Obtain an ST-Link STM32 programmer (v2 recommended, available on Amazon for less than $20).
2.  Download [STM32CubeProg](https://www.st.com/en/development-tools/stm32cubeprog.html) from [www.st.com](https://www.st.com).
    It is available for most platforms.
3.  Download the latest firmware `.hex` from this repo's
    [Releases](https://github.com/auxren/marf/releases). Use the standard image
    for the SAModular/EMS **v2** board; a separate `*-v1.6-no-strobe.hex` is
    provided for the original **v1.6** board (see
    [Hardware revisions](docs/02-installation-and-flashing.md#hardware-revisions-v2-vs-v16)).
4.  Flash the new firmware.
    1.  Power down the case.
    2.  Remove the module, leaving the power cable connected, and attach the S- Link ribbon cable.
    3.  Power on.
    4.  Program the firmware.
    5.  Power off, disconnect the ST-Link and reseat the module.
    6.  Power back on.
5.  The old cal data is now wrong, so run the calibration procedure below.

### Module Calibration

DIP switch settings

1.  Set switch 1 on for 1.2v/octave scaling.
2.  Set switch 2 on for 1v/octave scaling.
3.  Set switch 3 off always
4.  Set switch 4 on if an expander module is connected.

Due to some questionable hardware design choices, the module _absolutely requires_ calibration
for correct behavior of panel controls and external inputs. Use the following procedure.

1.  Hold Stage Address 1 Advance down during start up.
2.  Note leds cycling.
3.  Turn all pots to maximum. The slider positions do not matter.
4.  Apply a calibrated 10v source to all 4 external inputs.
5.  Select pulse 2 up if you need to swap the pulse leds. Led will move to pulse 2.
6.  Select pulse 1 up to swap it back to normal.
7.  Press Stage Address 2 Advance down to save calibration.
8.  It will ERASE the entire eprom, including saved programs.

(Some hardware revisions had the pulse leds switched by accident. 
You only need this part of the procedure if you are affected by this bug.)

### Usage

All of the instructions in the Buchla original manual still apply to the 2.66 revision
(perhaps more so than in 2.5). This section describes the additional functionality.

#### AFG Section Shift (unexpanded units only)

In normal operation, both function generators (AFG) utilize the same step programming.
This is how the original module operates. In previous revisions (1.0 - 2.5) this
was "improved" by making the programming distinct for each AFG. This was perhaps a
slight increase in functionality, with a decrease in usability. The original behavior
has been restored in 2.66. 

A hack is provided to access two different banks of step programming.

1.  Afgs 1 and 2 both access the same memory unless you use section shift.
2.  Press Stage Address Display 1 + Stage No Right to move afg1 to steps [17-32].
3.  Press Stage Address Display 1 + Stage No Left to move afg1 back to steps [1-16].
3.  Press Stage Address Display 2 + Stage No Right to move afg2 to steps [17-32].
4.  Press Stage Address Display 2 + Stage No Left to move afg2 back to steps [1-16].
5.  Switches have no effect when expander is present.

When set to steps 17-32, of course the slider value will still be that of 1-16.

#### Save Program

This version supports saving and loading 16 programs (even with expander). 
Slider data is always saved along with the program. Note that the marf does not 
stop running while saving a program.

1.  Press the Clear switch _down_ briefly to enter save mode. 
2.  The pulse leds will toggle to indicate that you are in save mode.
3.  To abort saving, press one of the Display switches.
4.  Use the Stage No switch to select one of the 16 memory locations.
    The step leds will indicate the memory location.
5.  Press Clear switch _down_ again briefly to save the program.
6.  Leds will flash downwards when the program is saved.

#### Load Program

Any of the 16 saved programs may be reloaded. Note that the marf does not stop 
running when loading a program. Once the program is loaded, all sliders will be 
"pinned" at their saved value. To restore slider activity, the slider must be moved 
through the saved value. If a slider is stuck then moving it from minimum to maximum
value will always unstick it.

1.  Press the Clear switch _up_ briefly to enter load mode.
2.  The pulse leds will toggle to indicate that you are in load mode.
3.  To abort loading, press one of the Display switches.
4.  Use the Stage No switch to select one of the 16 memory locations.
5.  Press Clear switch _up_ again briefly to load the program.
6.  Leds will flash upwards when the program is loaded.

#### Scales & Quantizing

Each sequence has its own **scale** and **root** for the quantizer (default
Chromatic). Hold the **Quantize** switch and move a **voltage** slider to pick
the scale, or a **time** slider to pick the root, for the displayed sequence.
See [Scales & quantizing](docs/06-scales.md).

#### Shift‑Register (Turing) Mode

Hold **Source External + Quantize** for ~0.8 s to toggle a per‑stage looping
shift‑register voltage generator for the displayed sequence. The four external
inputs become clocks, and each external‑source stage produces its own evolving,
quantizable CV. See [Shift‑register mode](docs/07-shift-register.md).

#### Pulse Tricks

Note these useful behaviors.

1.  A simultaneous pulse to start and stop is the same as the manual advance switch.
2.  A simultaneous pulse to strobe and start will correctly start on the strobed step.

To stack pulse inputs to multiple destinations, a hardware modification may be necessary
to increase the input impedence of the pulse inputs.

### Recommended Hardware Modifications

The module can also be improved by making several hardware modifications.
Visit [Dave Brown's Page](https://modularsynthesis.com/roman/buchla248/248_mods.htm)
for a comprehensive description of the recommended changes.

## Version History

This module's firmware has had a wild ride through a few generations.

### 3.0 line (this repo)

Building on v2.66, the firmware in this repository adds reliability fixes (an
independent watchdog, accurate cycle‑counter timing delays), checksummed/
versioned EEPROM storage, and new musical features driven entirely from the
existing panel: per‑sequence [scales and roots](docs/06-scales.md), a per‑stage
[shift‑register (Turing) mode](docs/07-shift-register.md), and soft‑normalled
external inputs. One source tree builds for both the **v2** and the original
**v1.6** boards. See the [user manual](docs/README.md) for full details.

### v2.66

The project was extensively rewritten by [maxl0rd](https://github.com/maxl0rd)
to more closely match the functionality of the original module,
and to resolve many performance problems.

See the [RELEASE_NOTES](https://github.com/wir35/marf/blob/v2.66/RELEASE_NOTES.txt) 
file for a description of both the user-visible and internal changes.

Expander modules provided by SAModular have been tested with this firmware
and operate correctly.

### v2.5

The project was converted from the Keil toolchain to STM32Cube by 
[Steven Barsky](https://github.com/stevenbarsky) and a good number of bugs were fixed,
including expander support.

Most new builds are currently programmed with this release.

### v1.0 and v2.0 Branches

These branches are the verbatim, original code drop from roman_f.

Released by roman_f on Oct 8, 2019.
See the [MuffWiggler Thread](https://www.muffwiggler.com/forum/viewtopic.php?t=222687).

Neither of these branches is verified, or known to work.
The hex firmware images checked in are not identical to the published firmware images
on the [Electronic Music Store build page](https://electricmusicstore.com/blogs/build/115318789-multiple-arbitrary-function-generator-model-248).

I do not recommend attempting to use these images for new builds.
