// Bit-banged open-drain I2C for the 200e preset bus. See i2c_bb.h for the
// contract and docs/DESIGN-200e-bus.md for the architecture rationale.

#if BUS200E_ENABLE

#include <stm32f4xx.h>

#include "i2c_bb.h"
#include "bus200e.h"     // BUS200E_EV_* event encoding
#include "delays.h"
#include "cycle_counter.h"
#include "marf_version.h"

// ---------------------------------------------------------------------------
// Pin mapping, derived entirely from the BUS200E_PINS_* selection in i2c_bb.h.
// ---------------------------------------------------------------------------
// SCL and SDA carry their own port, so a mapping may straddle GPIOA and
// GPIOB (the v2 default does). Where both land on one port the two sets are
// simply identical and the generated code is the same as before.
#if (BUS200E_PINS_PB3_PB4 + BUS200E_PINS_PA10_PB3 + BUS200E_PINS_PB3_PA10 + \
     BUS200E_PINS_PA9_PA10) != 1
#error "select exactly one BUS200E_PINS_* mapping"
#endif

#if BUS200E_PINS_PB3_PB4

#define SCL_GPIO        GPIOB
#define SCL_RCC         RCC_AHB1Periph_GPIOB
#define SCL_PORTSOURCE  EXTI_PortSourceGPIOB
#define SCL_PIN         GPIO_Pin_3
#define SCL_PINSOURCE   GPIO_PinSource3
#define SCL_EXTI        EXTI_Line3
#define SCL_IRQN        EXTI3_IRQn
#define SDA_GPIO        GPIOB
#define SDA_RCC         RCC_AHB1Periph_GPIOB
#define SDA_PORTSOURCE  EXTI_PortSourceGPIOB
#define SDA_PIN         GPIO_Pin_4
#define SDA_PINSOURCE   GPIO_PinSource4
#define SDA_EXTI        EXTI_Line4
#define SDA_IRQN        EXTI4_IRQn

#elif BUS200E_PINS_PA10_PB3

// TO COMPUTER header: pin 2 = UART_RX = PA10 (SCL), pin 4 = TDO = PB3 (SDA).
// SCL on EXTI15_10 costs nothing here: v2 sets MARF_PULSE_HAS_EXTI2_15 = 0,
// so that vector has no other tenant and the clamp-first ISR still enters
// with no dispatch check in front of it.
#if MARF_HW == 1
#error "PA10 is a DIP switch on the v1 board; this mapping is v2-only"
#endif
#define SCL_GPIO        GPIOA
#define SCL_RCC         RCC_AHB1Periph_GPIOA
#define SCL_PORTSOURCE  EXTI_PortSourceGPIOA
#define SCL_PIN         GPIO_Pin_10
#define SCL_PINSOURCE   GPIO_PinSource10
#define SCL_EXTI        EXTI_Line10
#define SCL_IRQN        EXTI15_10_IRQn    // free on v2
#define SDA_GPIO        GPIOB
#define SDA_RCC         RCC_AHB1Periph_GPIOB
#define SDA_PORTSOURCE  EXTI_PortSourceGPIOB
#define SDA_PIN         GPIO_Pin_3
#define SDA_PINSOURCE   GPIO_PinSource3
#define SDA_EXTI        EXTI_Line3
#define SDA_IRQN        EXTI3_IRQn

#elif BUS200E_PINS_PB3_PA10

// The same two header pins with clock and data swapped, for when nothing
// decodes and the orientation is the suspect.
#if MARF_HW == 1
#error "PA10 is a DIP switch on the v1 board; this mapping is v2-only"
#endif
#define SCL_GPIO        GPIOB
#define SCL_RCC         RCC_AHB1Periph_GPIOB
#define SCL_PORTSOURCE  EXTI_PortSourceGPIOB
#define SCL_PIN         GPIO_Pin_3
#define SCL_PINSOURCE   GPIO_PinSource3
#define SCL_EXTI        EXTI_Line3
#define SCL_IRQN        EXTI3_IRQn
#define SDA_GPIO        GPIOA
#define SDA_RCC         RCC_AHB1Periph_GPIOA
#define SDA_PORTSOURCE  EXTI_PortSourceGPIOA
#define SDA_PIN         GPIO_Pin_10
#define SDA_PINSOURCE   GPIO_PinSource10
#define SDA_EXTI        EXTI_Line10
#define SDA_IRQN        EXTI15_10_IRQn    // free on v2

#elif BUS200E_PINS_PA9_PA10

#if MARF_HW == 1
#error "PA9/PA10 are DIP switches on the v1 board; this mapping is v2-only"
#endif
#define SCL_GPIO        GPIOA
#define SCL_RCC         RCC_AHB1Periph_GPIOA
#define SCL_PORTSOURCE  EXTI_PortSourceGPIOA
#define SCL_PIN         GPIO_Pin_9
#define SCL_PINSOURCE   GPIO_PinSource9
#define SCL_EXTI        EXTI_Line9
#define SCL_IRQN        EXTI9_5_IRQn      // shared with the pulse inputs
#define SDA_GPIO        GPIOA
#define SDA_RCC         RCC_AHB1Periph_GPIOA
#define SDA_PORTSOURCE  EXTI_PortSourceGPIOA
#define SDA_PIN         GPIO_Pin_10
#define SDA_PINSOURCE   GPIO_PinSource10
#define SDA_EXTI        EXTI_Line10
#define SDA_IRQN        EXTI15_10_IRQn    // free on v2

#else
#error "select a BUS200E_PINS_* mapping in i2c_bb.h"
#endif

// Open-drain line control: BSRRH drives low (clamp), BSRRL releases (the bus
// pull-ups make the high level). IDR reads the actual line state.
#define SCL_READ()     ((SCL_GPIO->IDR & SCL_PIN) != 0)
#define SDA_READ()     ((SDA_GPIO->IDR & SDA_PIN) != 0)
#define SCL_DRIVE_LOW()  (SCL_GPIO->BSRRH = SCL_PIN)
#define SCL_RELEASE()    (SCL_GPIO->BSRRL = SCL_PIN)
#define SDA_DRIVE_LOW()  (SDA_GPIO->BSRRH = SDA_PIN)
#define SDA_RELEASE()    (SDA_GPIO->BSRRL = SDA_PIN)

#ifndef BUS200E_FORENSIC
#define BUS200E_FORENSIC 0
#endif
#if BUS200E_FORENSIC
// Bench forensics (gated; never shipped). The logic analyzer showed every
// truncated frame is the same failure: we assert the ACK, release SCL, and then
// never release the ACK, so the master stalls looking at SDA low with SCL high
// until our 1 ms failsafe frees it. These record WHICH path left the ACK
// asserted, rather than inferring it.
volatile uint8_t  fs_state, fs_in_ack, fs_gc, fs_bits, fs_scl, fs_sda;
volatile uint32_t fs_fires;                 // failsafe firings
volatile uint32_t fs_start_during_ack;      // START seen while we held the ACK
volatile uint32_t fs_stop_during_ack;       // STOP  seen while we held the ACK
volatile uint32_t fs_quiet_during_ack;      // went quiet while we held the ACK
volatile uint32_t fs_scl_isr;               // SCL ISR entries that we acted on
volatile uint32_t fs_scl_same;              // entries where the level had NOT changed
static volatile uint8_t fs_scl_last = 1;

// Per-edge trace of one frame. The bus is proven clean (SCL: 5280 ns high /
// 5120 ns low, zero runts) and a stock-firmware control runs 24/24 perfect
// frames, so the fault is in this state machine's own bookkeeping. Record what
// every SCL ISR SAW on entry, then freeze on the first failure so the trace
// belongs to the frame that actually broke.
//   bit 0    SCL level        bit 1    SDA level
//   bits 2-5 slv_bits         bits 6-7 slv_state
//   bits 8-9 slv_in_ack   bit 10 = the ISR's OWN second read of SCL
// High 16 bits: time since the previous edge, in 0.1 us units (saturating).
// Anchored at the START condition and capped at FS_TRACE_N. A full frame is
// 6 bytes x 9 clocks x 2 edges = 108 edges, so 128 holds an entire frame from
// its start, and freezing on the failsafe keeps the frame that actually broke.
#define FS_TRACE_N 128
volatile uint32_t fs_trace[FS_TRACE_N];   // low 16 = state, high 16 = dt in 0.1us
volatile uint32_t fs_trace_n;
volatile uint8_t  fs_trace_frozen;
static volatile uint32_t fs_t_prev;
#endif

volatile I2CBB_Stats i2cbb_stats;

// ---------------------------------------------------------------------------
// Slave event ring (ISR producer, superloop consumer).
// ---------------------------------------------------------------------------
#define EV_RING 128   // power of two; a GC command frame is ~10 events
static volatile uint16_t ev_ring[EV_RING];
static volatile uint8_t ev_w, ev_r;
static volatile uint8_t ev_ovf;

static void push_ev(uint16_t ev) {
  uint8_t next = (uint8_t) ((ev_w + 1) & (EV_RING - 1));
  if (next == ev_r) {
    ev_ovf = 1;   // delivered as BUS200E_EV_OVF once the ring drains
    i2cbb_stats.ring_overflows++;
    return;
  }
  ev_ring[ev_w] = ev;
  ev_w = next;
}

int I2CBB_GetSlaveEvent(uint16_t *ev) {
  if (ev_r != ev_w) {
    *ev = ev_ring[ev_r];
    ev_r = (uint8_t) ((ev_r + 1) & (EV_RING - 1));
    return 1;
  }
  if (ev_ovf) {
    ev_ovf = 0;
    *ev = BUS200E_EV_OVF;
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Slave state machine.
// ---------------------------------------------------------------------------
typedef enum {
  SLV_IDLE,   // waiting for START
  SLV_ADDR,   // shifting the address byte
  SLV_DATA,   // shifting general-call payload bytes
  SLV_QUIET,  // someone else's transaction: SCL EXTI masked until START/STOP
} SlaveState;

static volatile uint8_t slv_state = SLV_IDLE;
static volatile uint8_t slv_bits;    // data bits sampled this byte
static volatile uint8_t slv_shift;
static volatile uint8_t slv_in_ack;  // ACK clock cycle in progress
// Bench experiment gates (never ship). Suppressing our ACK is safe to TEST on a
// populated bus because the general-call ACK is the wired-OR of every slave --
// the 259e, 251e and CSR still ACK, so the master sees what it expects. With
// both off, frames complete 100%; with both on, ~55%. These split that in two.
#ifndef BUS200E_NO_ACK
#define BUS200E_NO_ACK 0
#endif
#ifndef BUS200E_ACK_ADDR
#define BUS200E_ACK_ADDR (!BUS200E_NO_ACK)   // ACK the general-call address byte
#endif
#ifndef BUS200E_ACK_DATA
#define BUS200E_ACK_DATA (!BUS200E_NO_ACK)   // ACK each payload byte
#endif
#define SDA_ACK_ADDR()  do { if (BUS200E_ACK_ADDR) SDA_DRIVE_LOW(); } while (0)
#define SDA_ACK_DATA()  do { if (BUS200E_ACK_DATA) SDA_DRIVE_LOW(); } while (0)

static volatile uint8_t slv_gc;      // current transaction is the general call

// Last SCL level we ACTED on. A real bus strictly alternates, so an interrupt
// reporting the level we already processed is for an edge that never happened.
// Bench-measured 2026-09-06: 7 such entries in 1253, i.e. 0.56%, which over a
// 108-edge frame is 0.6 per frame -- 45% of frames carry at least one, and the
// observed failure rate was 42-46%. They are glitches shorter than a logic
// analyzer's 80 ns sample period, invisible on a capture but long enough to
// set an EXTI pending bit. Each one samples a data bit twice or consumes a
// clock, which shifts every byte boundary after it: a captured failing frame
// read 00 04 00 44 2c where 00 04 00 22 16 was sent -- 0x44 = 0x22 << 1.
static volatile uint8_t  slv_scl_lvl = 1;   // the bus idles high
static volatile uint8_t  slv_sda_confirmed;  // SDA level that survived the confirm

// Cooperative ACK state (see i2c_bb.h). slv_probing marks a frame whose address
// ACK we deliberately did not drive, so the ACK clock's rising edge can tell us
// whether anyone else is out there.
volatile uint8_t  bus200e_peer_state = BUS200E_PEER_UNKNOWN;
volatile uint32_t bus200e_probe_count;
static volatile uint8_t  slv_probing;
static volatile uint8_t  slv_ack_this_frame;   // drive ACKs for the rest of this frame
static volatile uint16_t slv_frames_since_probe;

// Line-hold failsafe (design note: non-negotiable). TIM10 runs one-shot at
// 1 MHz and is armed for the whole time we hold EITHER line down: the SCL
// clamp, and -- just as important -- the ACK bit, during which SDA is driven
// low and is not released until the next SCL falling edge. If the master ever
// stops clocking mid-frame, a held ACK is the worse of the two failures: no
// device can raise SDA, so nobody on the bus can form a START or STOP and the
// whole case's preset bus is dead. Observed on the bench 2026-09-06, jammed by
// a 9-byte frame from another module that ended after four bytes.
// On expiry it force-releases both lines and goes quiet until a START or STOP
// resynchronizes us.
static void failsafe_arm(void) {
  TIM10->CNT = 0;
  TIM10->CR1 |= TIM_CR1_CEN;
}

static void failsafe_disarm(void) {
  TIM10->CR1 &= (uint16_t) ~TIM_CR1_CEN;
  TIM10->SR = (uint16_t) ~TIM_SR_UIF;
}

static void slave_go_quiet(void) {
#if BUS200E_FORENSIC
  if (slv_in_ack) fs_quiet_during_ack++;
#endif
  EXTI->IMR &= ~SCL_EXTI;
  slv_state = SLV_QUIET;
  slv_gc = 0;
  slv_in_ack = 0;
  i2cbb_stats.quieted++;
}

static void slave_listen_scl(void) {
  EXTI->PR = SCL_EXTI;      // stale edges from the masked period are not bits
  EXTI->IMR |= SCL_EXTI;
}

void I2CBB_SclIsr(void) {
  uint8_t scl;
  if (!(EXTI->PR & SCL_EXTI)) return;
  EXTI->PR = SCL_EXTI;

  // Read the line ONCE and decide everything from that value. Reading it again
  // further down is its own hazard: the bus rises in 720-960 ns and an
  // instruction takes 6 ns, so two reads inside one ISR can legitimately
  // disagree.
  scl = (uint8_t) (SCL_READ() ? 1 : 0);
  // Confirm the level before believing the edge. A slow SCL edge crosses the
  // input threshold more than once, and because chatter ALTERNATES (high, low,
  // high) a level-only filter passes every re-crossing -- which double-samples
  // a data bit and drifts the byte framing. See I2CBB_SCL_CONFIRM_NS.
  {
    uint32_t t0 = CLOCK_SOURCE_GET_TIMER();
    while ((CLOCK_SOURCE_GET_TIMER() - t0) < ((I2CBB_SCL_CONFIRM_NS * 168u) / 1000u)) { }
    if ((uint8_t) (SCL_READ() ? 1 : 0) != scl) {
      i2cbb_stats.glitches++;
      EXTI->PR = SCL_EXTI;       // swallow the re-crossing we just rode out
      return;
    }
  }

  // Alternation filter: a real bus strictly alternates, so an interrupt
  // reporting the level we already acted on is for an edge that never happened.
  // (A time-based filter was tried and REMOVED: the stamp is taken when the ISR
  // runs, not when the edge occurred, so ISR latency makes legitimate edges look
  // too close together and it rejected real ones -- 16/24 dropped to 12/24.)
  if (scl == slv_scl_lvl) {
    i2cbb_stats.glitches++;
    return;
  }
  slv_scl_lvl = scl;
#if BUS200E_FORENSIC
  {
    uint8_t lvl = (uint8_t) (SCL_READ() ? 1 : 0);
    fs_scl_isr++;
    if (lvl == fs_scl_last) fs_scl_same++;
    fs_scl_last = lvl;
    if (!fs_trace_frozen && fs_trace_n < FS_TRACE_N) {
      uint32_t now = CLOCK_SOURCE_GET_TIMER();
      uint32_t dt  = (now - fs_t_prev) / 17u;      /* 168 MHz -> ~0.1 us units */
      fs_t_prev = now;
      if (dt > 0xFFFFu) dt = 0xFFFFu;
      fs_trace[fs_trace_n] = (uint32_t) (lvl
                             | ((SDA_READ() ? 1u : 0u) << 1)
                             | ((slv_bits & 0xFu) << 2)
                             | ((slv_state & 3u) << 6)
                             | ((slv_in_ack & 3u) << 8)
                             /* the second read, as the branch below will see it */
                             | ((SCL_READ() ? 1u : 0u) << 10)
                             | (dt << 16));
      fs_trace_n++;
    }
  }
#endif

  if (slv_in_ack) {
    // While the ACK is asserted the edge MUST NOT be inferred from the pin
    // level. If this ISR runs late the line is already high again, the
    // level test below would take the "rising edge" branch and return, and
    // SDA would stay pinned low until the failsafe fired -- the master stalls
    // looking at SDA low with SCL high and abandons the frame. Every truncated
    // frame in the 2026-09-06 logic-analyzer capture was exactly this: a dead
    // gap of 996-997 us (the 1 ms failsafe) with SCL high and SDA held low.
    // Count the ACK clock's own edges instead of guessing from the level:
    //   slv_in_ack == 1  ACK asserted, waiting for the clock to go high
    //   slv_in_ack == 2  clock went high; the next SCL event ends the ACK
    // Arriving here with ==1 and SCL already low means we missed the rising
    // edge, so the high phase is over and releasing now is correct too.
    if (slv_in_ack == 1 && scl) {
#if BUS200E_COOP_ACK
      if (slv_probing) {
        // We did not drive this ACK. SDA low here means somebody else did, so
        // the bus has a peer that can acknowledge on our behalf. Sampled on the
        // rising edge, ~5 us after the falling edge, by which time a hardware
        // slave's 324 ns ACK is long since asserted.
        bus200e_peer_state = SDA_READ() ? BUS200E_PEER_ABSENT
                                        : BUS200E_PEER_PRESENT;
        slv_probing = 0;
        // If we are alone, start ACKing from the very next byte. This frame is
        // already lost (its address went unacknowledged) but the next will not
        // be.
        if (bus200e_peer_state == BUS200E_PEER_ABSENT) slv_ack_this_frame = 1;
      }
#endif
      slv_in_ack = 2;
      return;
    }
    // Drop the ACK. Clamp ONLY if SCL is genuinely still low: driving a line
    // that is already high manufactures a falling edge of our own, which fires
    // this very EXTI again, and the matching release fires it once more. The
    // spurious high entry then samples the same data bit twice and shifts every
    // byte boundary after it -- decoded frames come out as garbage. The
    // byte-boundary path below has always guarded this (late_falls); the ACK
    // release must too.
    if (!scl) {
      // Clamp SCL first so the master cannot begin the next bit, drop the ACK,
      // then hold the clock down for a fixed settle while SDA rises. See
      // I2CBB_ACK_SETTLE_US in i2c_bb.h for why this exists and why the wait is
      // a fixed time rather than a poll on SDA.
      uint32_t t0;
      failsafe_arm();
      SCL_DRIVE_LOW();
      SDA_RELEASE();
      t0 = CLOCK_SOURCE_GET_TIMER();
      while ((CLOCK_SOURCE_GET_TIMER() - t0) < (I2CBB_ACK_SETTLE_US * 168u)) { }
      SCL_RELEASE();
      failsafe_disarm();
    } else {
      // Late: the ACK bit is already over, so there is nothing to hold the
      // master off for. Just let go of SDA and touch nothing else.
      SDA_RELEASE();
      failsafe_disarm();
      i2cbb_stats.late_falls++;
    }
    slv_in_ack = 0;
    slv_bits = 0;
    slv_shift = 0;
    return;
  }

  if (scl) {
    // Rising edge: sample the data bit (SDA is stable while SCL is high).
    if ((slv_state == SLV_ADDR || slv_state == SLV_DATA) && slv_bits < 8) {
      slv_shift = (uint8_t) ((slv_shift << 1) | (SDA_READ() ? 1 : 0));
      slv_bits++;
    }
    return;
  }

  // Falling edge: byte boundaries. Clamp-first: stretch SCL before doing any
  // work, so the master cannot clock past us however late this ISR ran.
  if (slv_state != SLV_ADDR && slv_state != SLV_DATA) return;

  if (slv_bits != 8) return;   // mid-byte falling edge: nothing to do

  if (SCL_READ()) {   /* re-read deliberately: time has passed since entry */
    // We are so late that SCL is already high again -- clamping now would
    // corrupt the current clock. Give up on this byte's ACK window; the
    // START/STOP logic resynchronizes us on the next transaction.
    i2cbb_stats.late_falls++;
    return;
  }

  failsafe_arm();
  SCL_DRIVE_LOW();
  if (slv_state == SLV_ADDR) {
    if (slv_shift == 0x00) {
      // General call (write). ACK it and start collecting the frame.
      slv_gc = 1;
      slv_state = SLV_DATA;
      push_ev(BUS200E_EV_START);
      i2cbb_stats.gc_frames++;
#if BUS200E_COOP_ACK
      // Decide once per frame: probe, abstain, or ACK.
      {
        uint16_t due = (bus200e_peer_state == BUS200E_PEER_ABSENT)
                         ? BUS200E_PROBE_WHEN_ALONE : BUS200E_PROBE_WITH_PEERS;
        slv_probing = (bus200e_peer_state == BUS200E_PEER_UNKNOWN ||
                       slv_frames_since_probe >= due) ? 1u : 0u;
        if (slv_probing) {
          slv_frames_since_probe = 0;
          bus200e_probe_count++;
        } else {
          slv_frames_since_probe++;
        }
        // Drive only when we believe we are the sole slave and are not probing.
        slv_ack_this_frame =
            (uint8_t) (!slv_probing && bus200e_peer_state == BUS200E_PEER_ABSENT);
      }
      if (slv_ack_this_frame) SDA_ACK_ADDR();
#else
      slv_ack_this_frame = 1;
      SDA_ACK_ADDR();
#endif
      slv_in_ack = 1;
      SCL_RELEASE();
      // NO disarm here: SDA stays driven low for the whole ACK bit, and it is
      // only released on the next SCL falling edge. If the master stops
      // clocking in between, a held ACK jams the bus for every device on it
      // (nobody can raise SDA to make a START or STOP), which is worse than a
      // held clock. Leave the failsafe running until the ACK is released.
    } else {
      // Someone else's transaction (including any read): no ACK from us, and
      // no per-bit interrupts until the next START/STOP. This is what makes
      // other modules' multi-KB card streams nearly free.
      SCL_RELEASE();
      failsafe_disarm();
      slave_go_quiet();
    }
  } else {
    // Completed general-call payload byte.
    push_ev(slv_shift);
    i2cbb_stats.bytes++;
    if (slv_ack_this_frame) SDA_ACK_DATA();
    slv_in_ack = 1;
    SCL_RELEASE();
    // NO disarm here -- see the address-ACK path above.
  }
}

void I2CBB_SdaIsr(void) {
  if (!(EXTI->PR & SDA_EXTI)) return;
  EXTI->PR = SDA_EXTI;

  // SDA edges only mean START/STOP while SCL is high; edges during the low
  // phase are ordinary data setup (or our own ACK) and are ignored.
  if (!SCL_READ()) return;

  // Confirm the level before believing it. A slow SDA edge crosses the input
  // threshold several times (measured: 4-16 ns re-crossings on one edge), and
  // every re-crossing here would be read as a spurious START or STOP -- which
  // resets the frame, puts the bit counter in the wrong place, and ends with us
  // asserting an ACK at a moment that the master reads as a bus error. See
  // I2CBB_SDA_CONFIRM_NS for why this is a duration test and not a level test.
  {
    uint8_t v0 = (uint8_t) (SDA_READ() ? 1 : 0);
    uint32_t t0 = CLOCK_SOURCE_GET_TIMER();
    while ((CLOCK_SOURCE_GET_TIMER() - t0) < ((I2CBB_SDA_CONFIRM_NS * 168u) / 1000u)) { }
    if ((uint8_t) (SDA_READ() ? 1 : 0) != v0) {
      i2cbb_stats.glitches++;
      EXTI->PR = SDA_EXTI;      // swallow the re-crossing we just rode out
      return;
    }
    if (!SCL_READ()) return;    // the phase ended while we confirmed
    slv_sda_confirmed = v0;
  }

  slv_scl_lvl = 1;                                 // resync the edge tracker

  if (slv_sda_confirmed) {
    // STOP. Close any open general-call frame and return to full listening.
#if BUS200E_FORENSIC
    if (slv_in_ack) fs_stop_during_ack++;
#endif
    if (slv_gc) push_ev(BUS200E_EV_STOP);
    slv_gc = 0;
    if (slv_in_ack) { SDA_RELEASE(); failsafe_disarm(); }
    slv_in_ack = 0;
    if (slv_state == SLV_QUIET) slave_listen_scl();
    slv_state = SLV_IDLE;
    i2cbb_stats.stops++;
  } else {
    // START (or repeated START): a fresh address byte follows, so listen
    // again whatever we were doing. An open frame is abandoned; the parser
    // sees the new EV_START and drops the partial.
#if BUS200E_FORENSIC
    if (slv_in_ack) fs_start_during_ack++;
    if (!fs_trace_frozen) fs_trace_n = 0;   /* anchor the trace to this frame */
#endif
    if (slv_state == SLV_QUIET) slave_listen_scl();
    if (slv_in_ack) { SDA_RELEASE(); failsafe_disarm(); }
    slv_state = SLV_ADDR;
    slv_bits = 0;
    slv_shift = 0;
    slv_in_ack = 0;
    slv_gc = 0;
    slv_probing = 0;
    slv_ack_this_frame = 0;
    i2cbb_stats.starts++;
  }
}

// Failsafe: a clamp that was never released. Force-release both lines and go
// quiet until the bus resynchronizes us with a START or STOP.
void TIM1_UP_TIM10_IRQHandler(void) {
  if (!(TIM10->SR & TIM_SR_UIF)) return;
  TIM10->SR = (uint16_t) ~TIM_SR_UIF;
#if BUS200E_FORENSIC
  fs_trace_frozen = 1;      // keep the trace of the frame that broke
  fs_state = slv_state; fs_in_ack = slv_in_ack; fs_gc = slv_gc;
  fs_bits = slv_bits;
  fs_scl = (uint8_t) (SCL_READ() ? 1 : 0);
  fs_sda = (uint8_t) (SDA_READ() ? 1 : 0);
  fs_fires++;
#endif
  SCL_RELEASE();
  SDA_RELEASE();
  slave_go_quiet();
  i2cbb_stats.stretch_timeouts++;
}

// Dedicated EXTI vectors (shared-vector mappings get called from the sharing
// driver instead -- see i2c_bb.h).
#if BUS200E_PINS_PB3_PB4
void EXTI3_IRQHandler(void) { I2CBB_SclIsr(); }
void EXTI4_IRQHandler(void) { I2CBB_SdaIsr(); }
#elif BUS200E_PINS_PA10_PB3
void EXTI15_10_IRQHandler(void) { I2CBB_SclIsr(); }
void EXTI3_IRQHandler(void) { I2CBB_SdaIsr(); }
#elif BUS200E_PINS_PB3_PA10
void EXTI3_IRQHandler(void) { I2CBB_SclIsr(); }
void EXTI15_10_IRQHandler(void) { I2CBB_SdaIsr(); }
#elif BUS200E_PINS_PA9_PA10
// SCL (PA9, EXTI9) shares EXTI9_5 with the pulse inputs; main.c calls
// I2CBB_SclIsr() from that handler. SDA gets the (v2-free) EXTI15_10 vector.
void EXTI15_10_IRQHandler(void) { I2CBB_SdaIsr(); }
#endif

// ---------------------------------------------------------------------------
// Master (card transfers only; superloop context only).
// ---------------------------------------------------------------------------

static void master_suspend_slave(void) {
  EXTI->IMR &= ~(SCL_EXTI | SDA_EXTI);
  EXTI->PR = SCL_EXTI | SDA_EXTI;
  failsafe_disarm();
  slv_state = SLV_IDLE;
  slv_gc = 0;
  slv_in_ack = 0;
}

static void master_resume_slave(void) {
  SCL_RELEASE();
  SDA_RELEASE();
  slv_state = SLV_IDLE;
  EXTI->PR = SCL_EXTI | SDA_EXTI;
  EXTI->IMR |= SCL_EXTI | SDA_EXTI;
}

// Release SCL and wait for it to actually read high, tolerating other
// slaves' clock stretching up to the master timeout.
static int master_scl_high(void) {
  uint32_t t0 = get_millis();
  SCL_RELEASE();
  while (!SCL_READ()) {
    if (get_millis() - t0 > I2CBB_MASTER_TIMEOUT_MS) return I2CBB_ERR_TIMEOUT;
  }
  return I2CBB_OK;
}

static int master_wait_idle(void) {
  uint32_t t0 = get_millis();
  while (1) {
    uint32_t held = 0;
    while (SCL_READ() && SDA_READ()) {
      delay_us(1);
      if (++held >= I2CBB_BUSFREE_US) return I2CBB_OK;
    }
    if (get_millis() - t0 > I2CBB_MASTER_TIMEOUT_MS) return I2CBB_ERR_BUSY;
  }
}

static void master_start(void) {
  // Called with both lines released and the bus idle.
  SDA_DRIVE_LOW();
  delay_us(I2CBB_MASTER_HALF_US);
  SCL_DRIVE_LOW();
  delay_us(I2CBB_MASTER_HALF_US);
}

static int master_restart(void) {
  SDA_RELEASE();
  delay_us(I2CBB_MASTER_HALF_US);
  int r = master_scl_high();
  if (r != I2CBB_OK) return r;
  delay_us(I2CBB_MASTER_HALF_US);
  master_start();
  return I2CBB_OK;
}

static void master_stop(void) {
  SDA_DRIVE_LOW();
  delay_us(I2CBB_MASTER_HALF_US);
  if (master_scl_high() != I2CBB_OK) return;   // bus stuck; nothing more to do
  delay_us(I2CBB_MASTER_HALF_US);
  SDA_RELEASE();
  delay_us(I2CBB_MASTER_HALF_US);
}

// Shift one byte out, checking arbitration on every released (high) bit and
// returning the slave's ACK. On arbitration loss both lines are released and
// the caller must abandon the transfer without a STOP.
static int master_tx_byte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if ((b >> i) & 1) SDA_RELEASE(); else SDA_DRIVE_LOW();
    delay_us(I2CBB_MASTER_HALF_US);
    int r = master_scl_high();
    if (r != I2CBB_OK) return r;
    if (((b >> i) & 1) && !SDA_READ()) {
      SCL_RELEASE();
      SDA_RELEASE();
      return I2CBB_ERR_ARBLOST;
    }
    delay_us(I2CBB_MASTER_HALF_US);
    SCL_DRIVE_LOW();
  }
  // ACK clock: release SDA, sample.
  SDA_RELEASE();
  delay_us(I2CBB_MASTER_HALF_US);
  int r = master_scl_high();
  if (r != I2CBB_OK) return r;
  int acked = !SDA_READ();
  delay_us(I2CBB_MASTER_HALF_US);
  SCL_DRIVE_LOW();
  return acked ? I2CBB_OK : I2CBB_ERR_NACK;
}

static int master_rx_byte(uint8_t *out, int ack) {
  uint8_t b = 0;
  SDA_RELEASE();
  for (int i = 0; i < 8; i++) {
    delay_us(I2CBB_MASTER_HALF_US);
    int r = master_scl_high();
    if (r != I2CBB_OK) return r;
    b = (uint8_t) ((b << 1) | (SDA_READ() ? 1 : 0));
    delay_us(I2CBB_MASTER_HALF_US);
    SCL_DRIVE_LOW();
  }
  if (ack) SDA_DRIVE_LOW(); else SDA_RELEASE();
  delay_us(I2CBB_MASTER_HALF_US);
  int r = master_scl_high();
  if (r != I2CBB_OK) return r;
  delay_us(I2CBB_MASTER_HALF_US);
  SCL_DRIVE_LOW();
  SDA_RELEASE();
  *out = b;
  return I2CBB_OK;
}

int I2CBB_MasterWrite(uint8_t addr7, const uint8_t *pre, uint32_t pre_len,
                      const uint8_t *data, uint32_t data_len) {
  master_suspend_slave();
  int r = master_wait_idle();
  if (r == I2CBB_OK) {
    master_start();
    r = master_tx_byte((uint8_t) (addr7 << 1));
    for (uint32_t i = 0; r == I2CBB_OK && i < pre_len; i++)
      r = master_tx_byte(pre[i]);
    for (uint32_t i = 0; r == I2CBB_OK && i < data_len; i++)
      r = master_tx_byte(data[i]);
    if (r != I2CBB_ERR_ARBLOST) master_stop();
  }
  master_resume_slave();
  return r;
}

int I2CBB_MasterRead(uint8_t addr7, const uint8_t *pre, uint32_t pre_len,
                     uint8_t *data, uint32_t data_len) {
  master_suspend_slave();
  int r = master_wait_idle();
  if (r == I2CBB_OK) {
    master_start();
    r = master_tx_byte((uint8_t) (addr7 << 1));
    for (uint32_t i = 0; r == I2CBB_OK && i < pre_len; i++)
      r = master_tx_byte(pre[i]);
    if (r == I2CBB_OK) r = master_restart();
    if (r == I2CBB_OK) r = master_tx_byte((uint8_t) ((addr7 << 1) | 1));
    for (uint32_t i = 0; r == I2CBB_OK && i < data_len; i++)
      r = master_rx_byte(&data[i], i + 1 < data_len);   // NACK the last byte
    if (r != I2CBB_ERR_ARBLOST) master_stop();
  }
  master_resume_slave();
  return r;
}

// ---------------------------------------------------------------------------
// Init. Must run after mInterruptInit() (its EXTI_DeInit() would wipe this).
// ---------------------------------------------------------------------------
void I2CBB_Init(void) {
  GPIO_InitTypeDef gpio;
  EXTI_InitTypeDef exti;
  NVIC_InitTypeDef nvic;

  RCC_AHB1PeriphClockCmd(SCL_RCC | SDA_RCC, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM10, ENABLE);

  // Release both lines BEFORE switching to output-open-drain (ODR resets to
  // 0, which would clamp the bus during init).
  SCL_RELEASE();
  SDA_RELEASE();
  gpio.GPIO_Mode = GPIO_Mode_OUT;
  gpio.GPIO_OType = GPIO_OType_OD;      // only ever pull low; 5 V bus pull-ups
  gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
  gpio.GPIO_Speed = GPIO_Speed_25MHz;
  gpio.GPIO_Pin = SCL_PIN;
  GPIO_Init(SCL_GPIO, &gpio);
  gpio.GPIO_Pin = SDA_PIN;
  GPIO_Init(SDA_GPIO, &gpio);           // same port = same result as one call

  SYSCFG_EXTILineConfig(SCL_PORTSOURCE, SCL_PINSOURCE);
  SYSCFG_EXTILineConfig(SDA_PORTSOURCE, SDA_PINSOURCE);

  exti.EXTI_Line = SCL_EXTI | SDA_EXTI;
  exti.EXTI_Mode = EXTI_Mode_Interrupt;
  exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
  exti.EXTI_LineCmd = ENABLE;
  EXTI_Init(&exti);

  // Highest priority (with the ADC and pulse EXTIs): the clamp-first scheme
  // tolerates latency, but every cycle of it is jitter on the CV/pulse ISRs'
  // tail, so keep the bit ISRs short and un-preemptable.
  nvic.NVIC_IRQChannel = SCL_IRQN;
  nvic.NVIC_IRQChannelPreemptionPriority = 0;
  nvic.NVIC_IRQChannelSubPriority = 0;
  nvic.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvic);
  nvic.NVIC_IRQChannel = SDA_IRQN;
  NVIC_Init(&nvic);

  // Stretch failsafe: TIM10 on APB2 (168 MHz timer clock) at 1 MHz, one-shot.
  TIM10->CR1 = TIM_CR1_OPM | TIM_CR1_URS;
  TIM10->PSC = 167;
  TIM10->ARR = I2CBB_STRETCH_TIMEOUT_US;
  TIM10->EGR = TIM_EGR_UG;             // latch PSC/ARR
  TIM10->SR = (uint16_t) ~TIM_SR_UIF;
  TIM10->DIER = TIM_DIER_UIE;
  nvic.NVIC_IRQChannel = TIM1_UP_TIM10_IRQn;
  nvic.NVIC_IRQChannelPreemptionPriority = 1;
  nvic.NVIC_IRQChannelSubPriority = 0;
  NVIC_Init(&nvic);
}

#if BUS200E_DIAG
void I2CBB_DiagProbe(uint8_t *raw, uint8_t *pulled) {
  // PUPDR holds two bits per pin: 00 = none, 01 = pull-up. The pins are
  // configured with no pull, so restoring means clearing the field.
  const uint32_t scl_mask = 3u << (SCL_PINSOURCE * 2);
  const uint32_t scl_pu   = 1u << (SCL_PINSOURCE * 2);
  const uint32_t sda_mask = 3u << (SDA_PINSOURCE * 2);
  const uint32_t sda_pu   = 1u << (SDA_PINSOURCE * 2);
  *raw = (uint8_t) ((SCL_READ() ? 1 : 0) | (SDA_READ() ? 2 : 0));
  SCL_GPIO->PUPDR = (SCL_GPIO->PUPDR & ~scl_mask) | scl_pu;
  SDA_GPIO->PUPDR = (SDA_GPIO->PUPDR & ~sda_mask) | sda_pu;
  delay_us(50);   // ~40 k internal pull-up into pin + wire capacitance
  *pulled = (uint8_t) ((SCL_READ() ? 1 : 0) | (SDA_READ() ? 2 : 0));
  SCL_GPIO->PUPDR &= ~scl_mask;
  SDA_GPIO->PUPDR &= ~sda_mask;
}
#endif

#endif  // BUS200E_ENABLE
