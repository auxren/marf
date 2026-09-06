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
// Defaults: BENCH-VERIFIED 2026-08-13 on the v2 unit -- a weak-pull wiggle
// firmware + Saleae capture confirmed the "TO COMPUTER" header carries
// PA9/PA10 (solderless attachment), so v2 defaults to it. v1 keeps PB3/PB4
// (PA9/PA10 are its DIP switches; PB3/PB4 continuity still unverified there).
#if !defined(BUS200E_PINS_PB3_PB4) && !defined(BUS200E_PINS_PA9_PA10)
#if MARF_HW == 1
#define BUS200E_PINS_PB3_PB4   1
#else
#define BUS200E_PINS_PA9_PA10  1
#endif
#endif
#ifndef BUS200E_PINS_PB3_PB4
#define BUS200E_PINS_PB3_PB4   0   // SCL = PB3 (EXTI3), SDA = PB4 (EXTI4)
#endif
#ifndef BUS200E_PINS_PA9_PA10
#define BUS200E_PINS_PA9_PA10  0   // SCL = PA9 (EXTI9),  SDA = PA10 (EXTI10)
#endif

// Bring-up LED diagnostic (BUS200E_DIAG=1): the controller paints the bus pin
// levels and counters onto the step LEDs so bring-up needs neither SWD nor
// analyzer clips. Default 0; never ship enabled.
#ifndef BUS200E_DIAG
#define BUS200E_DIAG 0
#endif

// Timing parameters -- conservative first guesses, marked for verification on
// the logic analyzer against a real bus before they are trusted.
#define I2CBB_STRETCH_TIMEOUT_US 1000u  // failsafe force-release of clamped SCL
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
