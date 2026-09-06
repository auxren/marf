# 200e preset bus: wiring the MARF

This is the hardware rework that puts a MARF on the Buchla 200e preset bus, so
a preset manager (a 225e, a Studio H WPM, or anything else that speaks the bus)
can save and recall the module's 16 programs.

> **Status: not yet validated on hardware.** The firmware side is written and
> tested; the attachment point on the board is still being confirmed. Where a
> step is unverified this document says so. Do not treat it as a finished
> recipe yet.

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

This is the part that is still open. There are three candidates, easiest first.
Only one wire pair is needed.

### Option A: the "TO COMPUTER" header (v2 boards, solderless at the module)

The v2 board carries an unlabelled multi-pin header silkscreened **TO
COMPUTER**. A logic-analyzer capture on 2026-08-13 confirmed one of its pins is
**PA9**. The second pin of interest is either PA10 or PA11 and that is **not yet
resolved**. If it is PA10, this is the best option available: push two jumpers
onto the header and the only soldering is at the power connector.

If it turns out to be PA11, this option is dead on v2, because PA11 is the
1.2 V/oct DIP input.

> One pin on this header measured **−10.4 V** (the negative rail). Keep probes
> and jumpers off it.

- SCL → **PA9**
- SDA → **PA10**

Build the firmware with `BUS200E_PINS=pa910`.

### Option B: the programming header (solderless at the module)

The 2×10 boxed header marked **STLINK** is a standard 20-pin ARM debug
connector. On a standard pinout, **pin 13 is TDO (PB3)** and **pin 3 is nTRST
(PB4)** — both unused by this firmware, both 5 V tolerant. If the MARF board
actually routes those two positions, a two-wire IDC or DuPont plug does the job
with no soldering at the module.

**Not verified against the MARF board.** Beep it out before trusting it.

Note the tradeoff: the ST-Link and the bus wires then compete for one
connector, so you cannot debug and run on the bus at the same time.

- SCL → **pin 13 (PB3)**
- SDA → **pin 3 (PB4)**

Build with `BUS200E_PINS=pb34` (this is the default on v1/REV1).

### Option C: directly to the MCU legs (works on every board)

The fallback, and the only route on **v1 hardware**, where PA9 and PA10 are
already used as DIP switch inputs.

On the LQFP64 package, counting anticlockwise along the edge that ends at the
pin-1 corner: **leg 55 is PB3** and **leg 56 is PB4**. Useful anchors on the
same edge for orientation: leg 46 is SWDIO (PA13), leg 49 is SWCLK (PA14), and
leg 54 is PD2, which beeps out to the EEPROM chip select.

This is fine-pitch soldering onto a 0.5 mm pin. Use thin enamelled wire, tack
it down with strain relief, and beep each leg to its neighbours afterwards to
make sure you did not bridge anything.

Build with `BUS200E_PINS=pb34`.

---

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
make BUS200E_ENABLE=1 BUS200E_DIAG=1 BUS200E_PINS=pb34
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

Flash it, then **unplug the ST-Link** before reading the LEDs, since the
debugger drives some of these pins itself.

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
make BUS200E_ENABLE=1 BUS200E_RXLOG_ONLY=1 BUS200E_PINS=pb34
```

Same safety as the diagnostic build, without the LED overlay. Decoded commands
land in a debug ring readable over SWD, which is where to confirm that a
recall command from your preset manager arrives with the preset number you
expect.

### 6c. Live build: save and recall actually work

```
make BUS200E_ENABLE=1 BUS200E_PINS=pb34
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
- Bus presets **0 to 15** map to the module's 16 program slots. The bus preset
  space runs to 29; requests at 16 and above are logged and ignored, because
  the module has nowhere to put them.
- **Remote enable and disable** are honoured. While the bus has remote control
  disabled, save and recall commands are logged and ignored.
- **Card backup and restore** copy all 16 slots to and from a storage card.
  Restore checks every record's magic, version and checksum before letting it
  into the EEPROM, so a foreign or corrupt card cannot poison your programs.

Both paths run through the same code as the front panel, so a bus recall and a
panel recall leave the module in identical state.

---

## Known unknowns

These are the things still to be settled on real hardware. They are listed
because they change the wiring or the firmware, not because they are optional.

- **Which pins the TO COMPUTER header carries.** PA9 is confirmed; the second
  pin is PA10 or PA11, and that decides whether option A exists at all.
- **Whether the STLINK header routes TDO and nTRST**, which decides option B.
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
