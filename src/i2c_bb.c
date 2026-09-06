// Bit-banged open-drain I2C for the 200e preset bus. See i2c_bb.h for the
// contract and docs/DESIGN-200e-bus.md for the architecture rationale.

#if BUS200E_ENABLE

#include <stm32f4xx.h>

#include "i2c_bb.h"
#include "bus200e.h"     // BUS200E_EV_* event encoding
#include "delays.h"
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
static volatile uint8_t slv_gc;      // current transaction is the general call

// Stretch-timeout failsafe (design note: non-negotiable). TIM10 runs one-shot
// at 1 MHz; it is armed exactly while we clamp SCL and disarmed on release, so
// it can only ever fire if a bug leaves the clamp behind -- in which case it
// force-releases the bus rather than wedging the whole case's preset bus.
static void failsafe_arm(void) {
  TIM10->CNT = 0;
  TIM10->CR1 |= TIM_CR1_CEN;
}

static void failsafe_disarm(void) {
  TIM10->CR1 &= (uint16_t) ~TIM_CR1_CEN;
  TIM10->SR = (uint16_t) ~TIM_SR_UIF;
}

static void slave_go_quiet(void) {
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
  if (!(EXTI->PR & SCL_EXTI)) return;
  EXTI->PR = SCL_EXTI;

  if (SCL_READ()) {
    // Rising edge: sample the data bit (SDA is stable while SCL is high).
    if (!slv_in_ack && (slv_state == SLV_ADDR || slv_state == SLV_DATA) &&
        slv_bits < 8) {
      slv_shift = (uint8_t) ((slv_shift << 1) | (SDA_READ() ? 1 : 0));
      slv_bits++;
    }
    return;
  }

  // Falling edge: byte boundaries. Clamp-first: stretch SCL before doing any
  // work, so the master cannot clock past us however late this ISR ran.
  if (slv_state != SLV_ADDR && slv_state != SLV_DATA) return;

  if (slv_in_ack) {
    // End of the ACK clock: stop ACKing, set up for the next byte.
    failsafe_arm();
    SCL_DRIVE_LOW();
    SDA_RELEASE();
    slv_in_ack = 0;
    slv_bits = 0;
    slv_shift = 0;
    SCL_RELEASE();
    failsafe_disarm();
    return;
  }

  if (slv_bits != 8) return;   // mid-byte falling edge: nothing to do

  if (SCL_READ()) {
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
      SDA_DRIVE_LOW();
      slv_in_ack = 1;
      SCL_RELEASE();
      failsafe_disarm();
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
    SDA_DRIVE_LOW();
    slv_in_ack = 1;
    SCL_RELEASE();
    failsafe_disarm();
  }
}

void I2CBB_SdaIsr(void) {
  if (!(EXTI->PR & SDA_EXTI)) return;
  EXTI->PR = SDA_EXTI;

  // SDA edges only mean START/STOP while SCL is high; edges during the low
  // phase are ordinary data setup (or our own ACK) and are ignored.
  if (!SCL_READ()) return;

  if (SDA_READ()) {
    // STOP. Close any open general-call frame and return to full listening.
    if (slv_gc) push_ev(BUS200E_EV_STOP);
    slv_gc = 0;
    slv_in_ack = 0;
    if (slv_state == SLV_QUIET) slave_listen_scl();
    slv_state = SLV_IDLE;
    i2cbb_stats.stops++;
  } else {
    // START (or repeated START): a fresh address byte follows, so listen
    // again whatever we were doing. An open frame is abandoned; the parser
    // sees the new EV_START and drops the partial.
    if (slv_state == SLV_QUIET) slave_listen_scl();
    slv_state = SLV_ADDR;
    slv_bits = 0;
    slv_shift = 0;
    slv_in_ack = 0;
    slv_gc = 0;
    i2cbb_stats.starts++;
  }
}

// Failsafe: a clamp that was never released. Force-release both lines and go
// quiet until the bus resynchronizes us with a START or STOP.
void TIM1_UP_TIM10_IRQHandler(void) {
  if (!(TIM10->SR & TIM_SR_UIF)) return;
  TIM10->SR = (uint16_t) ~TIM_SR_UIF;
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
