#ifndef __I2C_BB_H
#define __I2C_BB_H

#include <stdint.h>

#include "marf_version.h"   // MARF_HW: the default attachment is per-revision

// ---------------------------------------------------------------------------
// Bit-banged open-drain I2C for the 200e preset bus (docs/DESIGN-200e-bus.md).
//
// Slave: receive-only on the I2C general-call address (0x00) with clamp-first
// clock stretching -- at every byte boundary the SCL edge ISR's first action
// is to clamp SCL low, so correctness depends on the ISR eventually running,
// not on its latency. Transactions addressed to anyone else are ignored
// cheaply (SCL interrupt masked until the next START/STOP). Decoded traffic
// is queued as BUS200E_EV_* events (bus200e.h) and drained from the superloop.
//
// Master: bus-idle-checked, arbitration-aware, used only for card
// backup/restore transfers and only ever called from the superloop
// (Bus200eTask), never from an ISR. The slave is suspended while mastering.
//
// The pins are configured open-drain with no pull-ups (the bus supplies 5 V
// pull-ups; FT pins read 5 V fine at 3.3 V VDD) and only ever pull low.
// ---------------------------------------------------------------------------

// Attachment point. Exactly one BUS200E_PINS_* is 1; every derived constant
// (port, EXTI lines, IRQ vectors, SYSCFG sources) follows from it in i2c_bb.c.
// Override from the command line (-DBUS200E_PINS_PB3_PB4=1) to force one.
//
// Defaults: taken from the manufacturer's schematic (Model 248 v2.5), which
// prints the LQFP64 pin numbers on the MCU symbol:
//   41 = PA8  = UART_CLK -> TO COMPUTER pin 1 AND DIP position 4 (expander)
//   42 = PA9  = no net   -> not connected anywhere on the board
//   43 = PA10 = UART_RX  -> TO COMPUTER pin 2                    (free)
//   55 = PB3  = TDO      -> TO COMPUTER pin 4 and STLINK pin 13  (free)
//   56 = PB4  = TRST     -> STLINK pin 3                         (free)
// The only free pins brought out to a connector are PA10, PB3 and PB4, and
// PA10 + PB3 sit two apart on the otherwise-unused TO COMPUTER header. That
// is the v2 default because it leaves the debug header alone: an attached
// ST-Link DRIVES nTRST (PB4), so a bus wire there fights the debugger, while
// PB3 on STLINK pin 13 is only TDO, an input on the debugger's side.
// v1 keeps PB3/PB4 (PA10 is one of its DIP switches; PB3/PB4 continuity on
// the v1 board is still unverified).
//
// Names read SCL_SDA. Both TO COMPUTER orientations are provided on purpose:
// swapped clock and data decode nothing at all, which on the LEDs is
// indistinguishable from a dead bus, so the other orientation is a rebuild
// rather than a resolder.
//   PA10_PB3: SCL = header pin 2, SDA = header pin 4   (default)
//   PB3_PA10: SCL = header pin 4, SDA = header pin 2
#if !defined(BUS200E_PINS_PB3_PB4) && !defined(BUS200E_PINS_PA9_PA10) && \
    !defined(BUS200E_PINS_PB3_PA10) && !defined(BUS200E_PINS_PA10_PB3)
#if MARF_HW == 1
#define BUS200E_PINS_PB3_PB4   1
#else
#define BUS200E_PINS_PA10_PB3  1
#endif
#endif
#ifndef BUS200E_PINS_PB3_PB4
#define BUS200E_PINS_PB3_PB4   0   // SCL = PB3  (EXTI3),  SDA = PB4  (EXTI4)
#endif
#ifndef BUS200E_PINS_PA10_PB3
#define BUS200E_PINS_PA10_PB3  0   // SCL = PA10 (EXTI10), SDA = PB3  (EXTI3)
#endif
#ifndef BUS200E_PINS_PB3_PA10
#define BUS200E_PINS_PB3_PA10  0   // SCL = PB3  (EXTI3),  SDA = PA10 (EXTI10)
#endif
#ifndef BUS200E_PINS_PA9_PA10
// Contradicted by the v2.5 schematic: PA9 has no net. An earlier Saleae
// capture read as PA9 was almost certainly PA8 on the adjacent leg. Kept only
// in case some other board revision routes it; never select it unverified.
#define BUS200E_PINS_PA9_PA10  0   // SCL = PA9 (EXTI9),  SDA = PA10 (EXTI10)
#endif

// Bring-up LED diagnostic (BUS200E_DIAG=1): the controller paints the bus pin
// levels and counters onto the step LEDs so bring-up needs neither SWD nor
// analyzer clips. Default 0; never ship enabled.
#ifndef BUS200E_DIAG
#define BUS200E_DIAG 0
#endif

// Timing parameters. The stretch timeout is MEASURED (Saleae Logic Pro 16 on a
// live 200e bus with a Studio H WPM, 2026-09-06); the rest are still guesses.
//
// Measured on that bus: SCL half-period 5.55 us mean (~90 kHz), a five-byte
// PRIMO frame ~600 us, and the longest legitimate stretch we ever impose is
// 17 us. The old 1000 us was ~90 bit times: when a fault did pin a line, the
// master had abandoned the frame long before the failsafe let go. 150 us is
// still 8x the worst real stretch, so it cannot fire spuriously, but it is
// under two byte times, which gives the master a chance to carry on rather
// than time out.
#define I2CBB_STRETCH_TIMEOUT_US 150u   // failsafe force-release of a held line
// After dropping an ACK, hold SCL low this long so SDA can actually rise before
// the master is allowed to start the next bit. MEASURED: SDA rise is 880 ns
// typical and 960 ns worst on this bus (96% of the I2C standard-mode budget),
// on top of a 0.56 us typical release latency.
//
// Why it is needed: a multi-master master monitors SDA while transmitting. If
// the next bit is a 1 it releases SDA and expects to read a 1; reading our 0 is
// arbitration loss and it abandons the frame. Proved on the bench 2026-09-06 --
// short V2 frames, which have no following byte, ran 30/30 while long PRIMO
// frames on the same firmware in the same minute ran 16/28.
//
// It must be a FIXED settle, not a wait for SDA to read high: when the master's
// next bit is a 0 it pulls SDA straight back down and a condition wait never
// completes (that version measured worse -- dead gaps went from ~20 to 80+).
#define I2CBB_ACK_SETTLE_US      2u
// How long an SDA level must persist before we believe it is a START or STOP.
// MEASURED at 500 MS/s on the live bus 2026-09-06: a slow SDA edge (880 ns
// typical, 960 ns worst -- 96% of the I2C standard-mode budget) crosses the
// input threshold more than once, giving runs of 4, 8, 10, 12 and 16 ns sitting
// on a single edge. At 12.5 MS/s these are invisible, which is why they were
// missed for so long.
//
// Alternation CANNOT filter chatter, because chatter alternates by definition:
// high, low, high all look like legitimate opposite edges. Only duration
// separates them. 400 ns is 25x the longest chatter run seen and a small
// fraction of the 5 us phase, so it cannot swallow a real START or STOP.
#define I2CBB_SDA_CONFIRM_NS   400u
// Same treatment for SCL, and this is the one that matters. Chatter on a rising
// edge reads as high, low, high -- three interrupts that ALTERNATE, so the
// level filter passes all three: we sample the bit, take a spurious falling
// path, then sample the SAME bit again. One extra bit of drift per chattering
// edge, after which we ACK on a data bit and the master loses arbitration and
// abandons the frame.
//
// PROVED on the bench 2026-09-06 at 500 MS/s: at the final ACK of every
// truncated frame (8 of 8) SDA was pulled down after 1421 ns -- our ISR
// latency -- while healthy ACKs on the same bus are pulled down in 324 ns by
// the hardware slaves. Nobody else was ACKing there, because for them it was
// not a byte boundary.
//
// Measured SCL chatter runs are 4-40 ns; 300 ns is over 7x that and a small
// fraction of the 5 us phase.
#define I2CBB_SCL_CONFIRM_NS   300u

#define I2CBB_MASTER_HALF_US     10u    // master half-bit (~50 kHz)
#define I2CBB_BUSFREE_US         100u   // both lines high this long = bus idle
#define I2CBB_MASTER_TIMEOUT_MS  5u     // cap on any single stretch/idle wait

// Master result codes
#define I2CBB_OK           0
#define I2CBB_ERR_BUSY    -1   // bus never went idle
#define I2CBB_ERR_ARBLOST -2   // lost arbitration to another master
#define I2CBB_ERR_NACK    -3   // slave did not ACK
#define I2CBB_ERR_TIMEOUT -4   // SCL held low past the stretch cap

// Call AFTER mInterruptInit(): that function's EXTI_DeInit() would wipe the
// bus EXTI configuration.
void I2CBB_Init(void);

// Drain one queued slave event (BUS200E_EV_* encoding); returns 0 when empty.
int I2CBB_GetSlaveEvent(uint16_t *ev);

// 24xx-EEPROM-style transfers to a storage card. `pre` holds the memory-offset
// bytes written before the data (write) or before the repeated START (read).
int I2CBB_MasterWrite(uint8_t addr7, const uint8_t *pre, uint32_t pre_len,
                      const uint8_t *data, uint32_t data_len);
int I2CBB_MasterRead(uint8_t addr7, const uint8_t *pre, uint32_t pre_len,
                     uint8_t *data, uint32_t data_len);

// Bring-up / debug counters (readable any time; written from ISRs).
typedef struct {
  uint32_t starts;            // START conditions seen
  uint32_t gc_frames;         // transactions addressed to the general call
  uint32_t bytes;             // payload bytes queued
  uint32_t stops;             // STOP conditions seen
  uint32_t quieted;           // transactions addressed to someone else
  uint32_t ring_overflows;    // slave events lost (superloop too slow)
  uint32_t stretch_timeouts;  // failsafe fired; must stay 0
  uint32_t late_falls;        // byte-boundary ISR ran after SCL rose again
  uint32_t glitches;          // SCL interrupts for an edge that never happened
} I2CBB_Stats;
extern volatile I2CBB_Stats i2cbb_stats;

// ISR bodies, exported for pin mappings whose EXTI vectors are shared with
// other drivers (PA9 lands in EXTI9_5 next to the pulse inputs). Each checks
// and clears its own EXTI pending bit; calling it when the line is not
// pending is a cheap no-op.
void I2CBB_SclIsr(void);
void I2CBB_SdaIsr(void);

#if BUS200E_DIAG
// Sample both lines as the MCU sees them, first with no pull (the normal
// configuration) and then with the internal pull-up briefly enabled. Bit 0 =
// SCL, bit 1 = SDA, set when the line reads high. A floating pin reads 0 then
// 1; a pin held low externally reads 0 both times; a pin on a live 5 V bus
// reads 1 both times. Superloop only.
void I2CBB_DiagProbe(uint8_t *raw, uint8_t *pulled);
#endif

#endif
