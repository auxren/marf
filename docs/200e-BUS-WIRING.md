# 200e preset bus: wiring the MARF

This is the hardware rework that puts a MARF on the Buchla 200e preset bus, so
a preset manager (a 225e, a Studio H WPM, or anything else that speaks the bus)
can save and recall the module's 30 programs.

> **Status: not yet validated on hardware.** The firmware side is written and
> tested, and the attachment pins are now confirmed against the Model 248 v2.5
> schematic, but no MARF has yet been seen decoding real bus traffic. Where a
> step is unverified this document says so.

---

## What the rework is

Two wires. No level shifter, no resistors, no cuts, no other parts.

The preset bus is 5 V open-drain I²C. The pull-up resistors already live on the
bus side, in the preset manager and the other modules, so the MARF only ever
pulls the lines low and never drives them high. The STM32F405 pins used here
are 5 V tolerant, so they sit on a 5 V bus at 3.3 V supply with nothing in
between. Ground is already shared through the module's power connector.

With **stock firmware** the two pins reset to floating or a weak pull-up, which
is passively safe on the bus. A reworked module therefore behaves normally on
the bus until you flash a firmware built with the bus enabled.

---

## Step 1: confirm your case carries the bus

The bus rides the module power connector on **pin 8 (SCL)** and **pin 9 (SDA)**.
In Buchla wiring these are the **yellow** and **green** conductors.

Before opening anything, confirm your case's busboard actually routes those two
pins to the slot the MARF lives in. 200e cases do. Older 200-series cases and
some third-party power distribution do not, and if the lines are not there the
rework will do nothing.

Quickest check: with the case powered and a preset manager installed, measure
pin 8 and pin 9 at the MARF's slot against ground. Both should sit at about
**5 V** at idle, because of the bus pull-ups. Two dead pins mean the bus does
not reach that slot.

---

## Step 2: pick the attachment point on the module

The Model 248 v2.5 schematic prints the LQFP64 pin numbers on the MCU symbol,
so the pins below are read off the manufacturer's drawing rather than guessed.

| MCU pin | Port | Net | Brought out to | Free? |
|---|---|---|---|---|
| 41 | PA8 | UART_CLK | TO COMPUTER pin 1, **and DIP position 4** | no, it is the expander switch |
| 42 | PA9 | *(none)* | nowhere | free but unreachable |
| 43 | PA10 | UART_RX | TO COMPUTER pin 2 | **yes** |
| 46 | PA13 | TMS | STLINK pin 7, DIP position 3 | no, SWDIO |
| 49 | PA14 | TCK | TO COMPUTER pin 3, STLINK pin 9 | no, SWCLK |
| 50 | PA15 | TDI | STLINK pin 5, DIP position 2 | no, the 1 V/oct switch |
| 55 | PB3 | TDO | TO COMPUTER pin 4, STLINK pin 13 | **yes** |
| 56 | PB4 | TRST | STLINK pin 3 | **yes** |

Three pins are both free and reachable: PA10, PB3 and PB4.

### Option A: the TO COMPUTER header (recommended)

The 2x5 header silkscreened **TO COMPUTER** carries only four signals, and two
of them are free and sit next to each other in the same column.

- **SCL (clock) to header pin 2** — UART_RX, PA10
- **SDA (data) to header pin 4** — TDO, PB3

Build with `BUS200E_PINS=pa10pb3`, which is also what plain `auto` selects on
v2. No soldering at the module: two jumpers push straight onto the header.

This is the recommendation because it leaves the debug header alone. See the
warning under option B for why that matters.

#### What to use

Two **female-to-female DuPont jumper wires**, the standard 0.1 inch (2.54 mm)
kind sold for breadboarding. One end pushes onto the header pin, the other end
goes to the wire you solder to the power connector. 20 cm is plenty; keep them
as short as the routing allows.

Two details worth getting right:

- **Use individual jumpers, not a ribbon strip.** The two pins you want are
  pin 2 and pin 4, which are adjacent in the same column, so a 2-way ribbon
  would fit — but pin 6 immediately below them is **+5 V**, and a strip is very
  easy to seat one row off. Separate wires make that mistake obvious.
- **Colour them and keep the colours consistent with the Buchla wiring**:
  yellow for clock (pin 2), green for data (pin 4). The power connector uses
  the same convention, so the whole run reads the same at both ends.

Header pin numbering on a 2x5 IDC header: pin 1 is marked on the silkscreen
(usually a square pad or a triangle), odd pins run down one column and even
pins down the other, so pin 2 is beside pin 1 and pin 4 is directly below
pin 2.

```
      TO COMPUTER
   pin 1  o  o  pin 2   <- SCL / clock  (PA10)   yellow
   pin 3  o  o  pin 4   <- SDA / data   (PB3)    green
   pin 5  o  o  pin 6      +5 V   -- keep off
   pin 7  o  o  pin 8
   pin 9  o  o  pin 10     -15 V  -- keep off
```

If your jumpers are loose on the pins, crimp the female shells gently with
pliers before fitting. A jumper that falls off mid-performance looks exactly
like a dead bus.

> Header pin 6 is **+5 V** and sits directly below pin 4, and header pin 10 is
> **−15 V**. Keep probes and jumpers off both.

If nothing decodes later, suspect the orientation before suspecting the bus.
Swapped clock and data produce no decode at all, which on the LEDs is
indistinguishable from a dead bus. Rebuild with `BUS200E_PINS=pb3pa10` to try
the other way round rather than resoldering.

### Option B: the programming header

The 2x10 boxed header marked **STLINK** is a full standard 20-pin ARM JTAG
connector, confirmed against the schematic: **pin 3 is TRST (PB4)** and **pin
13 is TDO (PB3)**.

- SCL to **pin 13 (PB3)**
- SDA to **pin 3 (PB4)**

Build with `BUS200E_PINS=pb34`. This is the default on v1 and REV1.

> **Do not leave the ST-Link plugged in with this option.** nTRST is an
> *output* from the debugger, so an attached ST-Link actively drives your SDA
> line and fights the bus. Option A avoids this entirely: PB3 also appears on
> STLINK pin 13, but that is TDO, an input on the debugger's side, so it is
> harmless.

### Option C: directly to the MCU legs

The fallback, and the only route on **v1 hardware**, where PA10 is a DIP switch
input.

On the LQFP64 package, counting anticlockwise along the edge that ends at the
pin-1 corner: **leg 55 is PB3** and **leg 56 is PB4**. Useful anchors on the
same edge: leg 46 is SWDIO (PA13), leg 49 is SWCLK (PA14), and leg 54 is PD2,
which beeps out to the EEPROM chip select.

This is fine-pitch soldering onto a 0.5 mm pin. Use thin enamelled wire, tack
it down with strain relief, and beep each leg to its neighbours afterwards to
make sure you did not bridge anything.

Build with `BUS200E_PINS=pb34`.

### Not an option: PA9

Earlier revisions of this document proposed PA9 as an attachment point, on the
strength of a logic-analyzer capture. The schematic shows **PA9 has no net at
all**, so that capture was almost certainly reading PA8 on the adjacent leg.
The `pa910` build option still exists in case some other board revision routes
PA9, but do not select it without verifying continuity first.

## Step 3: verify the pins are free before you wire them

Whichever option you chose, confirm with a continuity tester that the two pins
are not already connected to something on the board. They should read open to
the 3.3 V rail, to the 5 V rail, and to the ground plane.

---

## Step 4: run the two wires

Solder to the back of the power connector at **pin 8** and **pin 9** and run
them to the two pins you chose.

- Power connector **pin 8 (yellow, SCL)** → your SCL pin
- Power connector **pin 9 (green, SDA)** → your SDA pin

For option A the easiest build is: solder a short length of hookup wire to
power connector pins 8 and 9, then solder the **male** end of a female-to-female
DuPont jumper to each, or crimp a male pin on and mate it. That leaves the only
soldering at the power connector, where there is room to work, and the module
end stays a plug you can pull.

Keep the wires short and away from the analog section. Ground is already
common through the power connector, so do not add a ground wire.

**Do not swap SCL and SDA.** Nothing will be damaged, but the module will
decode nothing at all, which looks exactly like a dead bus.

---

## Step 5: check the wiring before flashing bus firmware

Power the case with stock firmware still on the module. Both wires should idle
at about **5 V**. If either sits near 0 V, something is holding it low, and you
should find that before going further. If either floats at an indeterminate
voltage, the bus is not reaching the slot.

---

## Step 6: bring-up firmware, in order

Do not jump straight to a build that can write to your presets. There are three
stages, and each one only proves the next is worth trying.

### 6a. Diagnostic build: prove the bus reaches the MCU

```
make BUS200E_ENABLE=1 BUS200E_DIAG=1 BUS200E_PINS=pa10pb3
```

This build is **receive-only** — it can never touch a saved preset — and it
paints the bus state onto the 16 step LEDs, so you need neither a debugger nor
a logic analyzer:

| LED | Meaning when lit |
|-----|------------------|
| 1, 2 | SCL, SDA reading high with no internal pull |
| 3, 4 | SCL, SDA reading high with the internal pull-up on |
| 5 | heartbeat, blinks about once a second |
| 6 | stretch failsafe fired (must stay dark) |
| 7 | a clock edge was serviced late |
| 8 | bus traffic seen for other addresses |
| 9–12 | count of START conditions, low nibble, LED 9 is the least significant bit |
| 13–16 | count of general-call command frames, low nibble, LED 13 is the least significant bit |

Flash it, then read the LEDs. With **option A** the debugger shares no line
with the bus, so it can stay plugged in. With **option B or C** you must
**unplug the ST-Link** first, because it drives nTRST and will make a dead bus
look like a live one.

Read LEDs 1 to 4:

- **All four lit** — the wires are on the bus and the bus is pulled up. Good.
- **1 and 2 dark, 3 and 4 lit** — the pins are floating. Your wires are not
  landing where you think they are. Go back to step 2.
- **All four dark** — something is holding the lines low.

Then recall a preset from your preset manager and watch LEDs 9 to 16 count. If
LED 8 lights but the frame counter never moves, the bus is alive but the
manager is not addressing commands the way this firmware expects.

### 6b. Receive-only build: watch real commands, still no writes

```
make BUS200E_ENABLE=1 BUS200E_RXLOG_ONLY=1 BUS200E_PINS=pa10pb3
```

Same safety as the diagnostic build, without the LED overlay. Decoded commands
land in a debug ring readable over SWD, which is where to confirm that a
recall command from your preset manager arrives with the preset number you
expect.

### 6c. Live build: save and recall actually work

```
make BUS200E_ENABLE=1 BUS200E_PINS=pa10pb3
```

Only flash this once 6a or 6b has shown real command frames arriving. This
build acts on them, and a save command overwrites a program slot.

---

## What works once it is live

- **Recall preset N** loads program slot N into the running module, exactly as
  the front-panel load gesture does.
- **Save preset N** writes the running program into slot N, exactly as the
  front-panel save gesture does, and marks the slot as yours so a factory bank
  update will not overwrite it.
- Bus presets **0 to 29** map one-to-one onto the module's 30 program slots, so
  a preset manager addresses the MARF exactly as it would any other 200e
  module. The first 16 slots ship with the factory bank; slots 17 to 30 start
  empty.
- **Remote enable and disable** are honoured. While the bus has remote control
  disabled, save and recall commands are logged and ignored.
- **Card backup and restore** copy all 30 slots to and from a storage card.
  Restore checks every record's magic, version and checksum before letting it
  into the EEPROM, so a foreign or corrupt card cannot poison your programs.

Both paths run through the same code as the front panel, so a bus recall and a
panel recall leave the module in identical state.

---

## Current state on real hardware

As of 2026-09-06, on a v2 unit in a 200e case with a Studio H WPM:

**Working.** The bus reaches the MCU, both frame formats decode, remote enable
and disable are honoured, and a preset recall from the WPM loads the addressed
program into the running module. Bus preset 12 on the WPM loads MARF program
12 (the wire carries 11; the manager's panel is 1-indexed and the wire is
0-indexed).

**Not yet reliable.** Roughly half the command frames are decoded; the rest are
dropped and retried. Nothing is corrupted when a frame is lost — the parser
discards partial frames and the failsafe releases the lines — but you may have
to send a recall more than once. This is a firmware issue in the bit-banged
slave, not a wiring one, and it is being worked on. Do not read a missed recall
as a bad solder joint.

**Fixed, and worth knowing about if you are on older firmware.** An earlier
build could hold SDA low indefinitely and jam the preset bus for the whole
case. If a module ever does that, every other module goes silent too, because
nothing on the bus can signal a start or stop while one device pins the data
line. Power-cycling the offending module clears it.

## Known unknowns

These are the things still to be settled on real hardware. They are listed
because they change the wiring or the firmware, not because they are optional.

- **Whether the wires actually reach a live bus.** Every pin above is confirmed
  from the schematic, but no MARF has yet been observed decoding a real preset
  bus. Step 6a is what settles it.
- **The module's own bus address.** `BUS200E_MODULE_ADDR` currently defaults to
  0x3C, chosen only to avoid every address seen in the published preset dumps.
  It is not confirmed against a real system's enumeration, and card backup and
  restore commands are addressed to it, so it must be right before those are
  trusted.
- **Bus timing constants** in `src/i2c_bb.h` are conservative first guesses and
  have not been checked against a real bus on a logic analyzer.
- **Storage-card wire format** in `src/bus200e_ops.c`: address width, page size
  and write-cycle time are the standard 24xx-series values, not measured
  against an actual card.
