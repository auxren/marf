# 200e preset-bus attachment (design note, 2026-08-13 — not yet implemented)

## STATUS (2026-08-13, session handoff)

- **Decided & verified:** bus = multi-master I²C @100 kHz, **5 V (owner-measured)**, EDAC
  pins 8 (SCL)/9 (SDA); protocol fully documented via the 2WIRELESS source (below); MARF
  attachment = **bit-banged open-drain I²C, no level shifter** (FT pins tolerate 5 V; we
  only ever pull low). Owner's board photo confirms a **v2 board** (CTS 208-4 rear DIP
  labeled 2V/1.2V / 1V/OCT / EXPANDER; STM32F405RG LQFP64; the EEPROM is a Microchip
  **25AA512** — the `CAT25512.c` driver name is just the equivalent part).
- **Open NOW (owner has DMM + board out): pick the attachment point** — three candidates
  in priority order, see "Pins & electrical". Two of them are solderless. Run the beep
  checklist, then lock the choice into this doc.
- **Then:** implement `src/i2c_bb.c` + `src/bus200e.c` per the architecture below —
  host-testable, repo house style (StdPeriph, `make test` suite), gated `BUS200E_ENABLE=0`.
- Companion work: the 288r repo has the same bus attachment designed (shared-codec-I2C1 +
  BSS138 shifter variant) in its `firmware/DESIGN.md`; protocol notes are duplicated here
  so this repo stands alone.

## Board headers (identified from owner photo, 2026-08-13)

- **2×10 boxed IDC ("STLINK")** = the programming header, ST-Link/V2 native 20-pin ribbon.
  If wired to the full standard ARM 20-pin JTAG pinout, it ALSO breaks out our bus pins:
  **pin 13 = TDO = PB3, pin 3 = NTRST = PB4** → solderless attachment via an IDC plug.
- **2×5 boxed IDC ("TO COMPUTER")** = undocumented (not in firmware — no UART/USB/host
  link exists in src — nor on Dave Brown's 248 build page). Leading hypothesis: **USART1
  breakout, PA9/PA10** (exactly the pins freed in the v2 DIP rework; the name fits a
  host-serial link; probably factory-test/vestigial). If confirmed, PA9/PA10 are 5V-tolerant
  and bit-bang fine → this header becomes the best attachment (solderless + keeps PB3/PB4).
- **2×8 boxed IDC ("EXPANDER")** = the expander link (`expander.c`).

## Beep checklist (LQFP64 leg numbers; do this before any wiring)

Anchors: SWDIO PA13 = leg 46, SWCLK PA14 = leg 49, PD2 (EEPROM CS, beeps to the
25AA512's CS pin) = leg 54. Counting runs counterclockwise; legs 49–64 are the side
ending at the pin-1 corner: 49=PA14, 50=PA15, 51–53=PC10–12, 54=PD2, **55=PB3, 56=PB4**.

1. [ ] TO COMPUTER pins ↔ leg 42 (PA9) and leg 43 (PA10). Hit → use this header
   (map its full pinout: also beep each pin to GND plane / 3V3 / 5 V rails; record here).
2. [ ] STLINK pin 13 ↔ leg 55 (PB3) and pin 3 ↔ leg 56 (PB4). Hit → IDC-plug attachment.
3. [ ] Fallback: solder to legs 55/56 directly (verify unconnected first: beep to
   surrounding pads; magnet wire + flux + strain relief).
4. [ ] Whichever pins win: confirm the case busboard routes EDAC 8/9 at the MARF slot;
   after wiring, both lines idle ~5 V at power-on with stock firmware (pins reset to
   float (PB3/PA9/PA10) or weak pull-up (PB4) — passively safe on the bus).

If the attachment lands on PA9/PA10, update "Pins & electrical" below (written for
PB3/PB4) — the driver architecture is pin-agnostic; only the GPIO/EXTI init changes.

Goal: the MARF joins the Buchla 200e preset bus — presets save/recall from a
225e/206e Preset Manager alongside real 200e modules, card backup/restore maps
onto the existing CAT25512 preset records, and (phase 2) the module hears bus
MIDI (note/clock). Companion design: the 288r repo's `firmware/DESIGN.md`
"200e preset-bus attachment" section (same bus, different attachment strategy).

## The bus (established facts)

- Plain **multi-master I²C @ 100 kHz, 5 V** (owner-confirmed level) on the EDAC
  power connector: **pin 8 = SCL (yellow), pin 9 = SDA (green)**. Pull-ups live
  on the system side. Bus lockups in large systems are a known failure mode —
  design for recovery.
- Protocol is public in **github.com/studiohsoftware/2WIRELESS** (Studio H
  Wireless Preset Manager). Summary of the wire protocol:
  - **Commands = I²C writes to GENERAL CALL address 0x00**; module identity is a
    payload byte, not the wire address. PRIMO framing: `0x00 N` = recall preset
    N (0–29), `0x01 N` = save, `0x14`/`0x15` = remote enable/disable,
    `0x2D`/`0x2E modAddr memLSB memMSB cardLo` = dump/restore all presets
    to/from card, `0x80/0x90|busmask note velo` = MIDI note, `0xB0|busmask`
    = CC, `0xF8/0xFA/0xFC` = MIDI clock/start/stop. Pre-PRIMO framing wraps the
    same ops as `[nBytes, 0x00, 0x22, subcmd, args…]` — support RX of both.
  - **Storage cards slave at `0x50|cardLo` and behave like a 24xx EEPROM**:
    on `0x2D`/`0x2E` addressed to us, *the module becomes bus master*, writes a
    2–3-byte big-endian memory offset, then streams (backup) or
    sequential-reads (restore) its preset blob. Blob format is per-module and
    **opaque to the card** → our blob = the existing versioned/CRC preset
    records, verbatim.
  - Known module payload addresses: 0x28 = 259, 0x44 = 291e. Full roster
    unpublished — pick `BUS200E_MODULE_ADDR` after enumerating a real system
    (the 225e displays a module's address on remote-enable hold).

## Why bit-bang (no hardware I²C available)

Every I²C-capable pin of the F405R (LQFP64: PB6/7, PB8/9, PB10/11, PA8, PC9) is
committed on both board revs (pulse inputs, pulse LEDs, DIPs, step-storage LED),
and the firmware uses no I²C anywhere — there is no existing bus to piggyback
(unlike the 288r's codec net). So: **bit-banged I²C on spare GPIOs.**

The MARF can afford this where an audio module couldn't: every ISR in the module
is control-rate, so a top-priority ~1–2 µs SCL edge interrupt adds only
occasional microsecond jitter to CV updates. Bus traffic is sparse (preset
commands = a few bytes; worst case = another module's multi-KB card restore,
which we ignore after the address byte).

## Pins & electrical

- **PB3 (SCL), PB4 (SDA)** — referenced nowhere in the firmware (JTAG TDO/NTRST
  defaults on an SWD-only design), both 5V-tolerant FT pins. Backups if the
  board disagrees: PA9/PA10 (freed in the v2 DIP rework; v1 used them as DIPs).
  Avoid PB2 (BOOT1 strap).
- **No level shifter needed.** Bit-banged I²C is open-drain: the pin only ever
  pulls low; the bus's 5 V pull-ups make the high level, and FT inputs read 5 V
  fine at 3.3 V VDD. Configure both pins open-drain, no internal pull-ups,
  never drive high. (The 288r needs a BSS138 shifter only because its codec
  shares the net; nothing shares these pins here.)

Wire-up checklist:
1. [ ] Verify PB3/PB4 are unconnected on the physical board (source says
   unused; confirm no PCB tie). Check whether PB3 (SWO) already routes to the
   debug header — if so, one wire is pre-broken-out.
2. [ ] Confirm the case busboard routes EDAC pins 8/9 to the MARF's position.
3. [ ] Two flywires: PB3 → EDAC pin 8 (SCL), PB4 → EDAC pin 9 (SDA). Common
   ground already shared via the power connector.
4. [ ] Power on with stock firmware (pins default to inputs): DMM the bus —
   idle high ~5 V on both lines, module present, nothing loaded.

## Firmware architecture (`src/bus200e.c` + `src/i2c_bb.c`)

**Slave RX — clamp-first clock stretching.** The classic bit-bang-slave latency
problem is dissolved by stretching: the SCL falling-edge EXTI's *first action*
is to clamp SCL low (pin → output-low), then decode the bit at leisure, then
release. Correctness depends on the ISR eventually running, not on its latency.
The 200e bus provably tolerates stretching (2WIRELESS's hardware I²C stretches
by default and manipulates it explicitly).

- SCL EXTI at **highest NVIC priority**, tiny (clamp + shift + release); SDA
  EXTI watches START (SDA↓ while SCL high) and STOP (SDA↑ while SCL high).
- ACK general call 0x00 only; on any other address, go quiet until STOP
  (drop to SDA-edge monitoring — this is what makes other modules' card
  streams nearly free).
- **Stretch-timeout failsafe (non-negotiable):** a hardware timer force-releases
  SCL after ~1 ms clamped, unconditionally. A bug that wedges SCL kills the
  whole case's preset bus until power-cycle; this must be impossible.

**Master (card transfers) — easy half.** We own the clock: bus-idle wait
(both lines high ≥ a bus-free time), clock at ~50 kHz, monitor SDA after
releasing each bit for arbitration loss (abort + retry with backoff). EEPROM
protocol to `0x50|cardLo`: offset write then stream, exactly as 2WIRELESS
implements.

**Dispatch (host-testable, BSP-free like the rest of the codebase):**
byte-stream parser (both framings) → `remote_enabled` state (0x14/0x15,
default-enabled at boot, verify real-225e etiquette), save/recall N → the
existing preset slots (N beyond our slot count: ignore — decide mapping at
implementation), 0x2D/0x2E for our address → superloop job that runs the
master transfer (never inside an ISR). Everything behind `BUS200E_ENABLE`
(default 0); first enabled build is RX-log-only into a debug ring.

**Phase 2:** bus MIDI RX — clock as an external-clock source candidate
(clockfollow already exists), notes/CC uses TBD.

## Test plan without a 225e

Any Arduino/Pi drives the whole protocol: send general-call frames, emulate the
card at 0x50 (~40 lines of Wire slave code). Bring-up wants the logic analyzer
on SCL/SDA from day one — slave bit-bang is the most delicate firmware in the
module and earns its analyzer time.

## Open items

- [ ] PB3/PB4 board continuity check (item 1 above).
- [ ] Case busboard routes pins 8/9 at the MARF position.
- [ ] Pick `BUS200E_MODULE_ADDR` (enumerate a real system; avoid 0x28, 0x44).
- [ ] Remote-enable default-state etiquette on a real 225e.
- [ ] Preset-slot mapping for bus presets 0–29 vs our slot count.
- [ ] 206e quirks — 2WIRELESS has special startup handling ("free 206e when
  starting up"); read that path before testing against a 206e.
