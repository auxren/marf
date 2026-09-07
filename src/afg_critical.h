#ifndef __AFG_CRITICAL_H
#define __AFG_CRITICAL_H

#include <stm32f4xx.h>
#include "marf_version.h"
#include "i2c_bb.h"   // BUS200E_ENABLE

// ---------------------------------------------------------------------------
// Critical sections around AFG state.
//
// These used __disable_irq(), which masks EVERYTHING, and that breaks the 200e
// bus slave. A bit-banged slave survives a LATE interrupt but not an ABSENT
// one: during a global disable, SCL goes low then high -- two edges -- while
// EXTI latches only one pending bit, so one ISR is serviced for two
// transitions and exactly one data bit is lost. Every byte after it is then
// shifted, which is why a captured failing frame read 00 04 00 44 2c where
// 00 04 00 22 16 was sent (0x44 = 0x22 << 1).
//
// Measured on the bench 2026-09-06: the AFG tick handler held interrupts off
// for 11.09 us against a 5 us half-period, and the trace of a broken frame
// shows a 14.6 us gap between consecutive SCL interrupts where 5 us was due.
//
// What actually needs excluding is only the handlers that touch AFG state.
// The 200e bus EXTIs are deliberately absent from both masks: they touch only
// the I2C lines, the transport event ring and i2cbb_stats.
//
//   TIMER  - inside TIM4/TIM5. They share a preemption priority and so cannot
//            preempt each other, which is what keeps their SPI2 DAC writes
//            atomic; that was never the job of __disable_irq().
//   THREAD - the superloop, which everything can preempt, so the tick timers
//            are masked too.
// EXTI0/EXTI1 sit below the timers and cannot preempt them, so they appear
// only in the THREAD mask.
//
// On v1 the pulse strobes occupy EXTI2 and EXTI15_10 and must be masked; on v2
// they do not, and EXTI15_10 is where the bus SCL lives, so masking it there
// would reintroduce the exact bug this replaces. MARF_PULSE_HAS_EXTI2_15 draws
// that line.
// ---------------------------------------------------------------------------

#define AFG_CRIT_TIMER_M0 ((1u << ADC_IRQn) | (1u << EXTI9_5_IRQn) \
                           | (MARF_PULSE_HAS_EXTI2_15 ? (1u << EXTI2_IRQn) : 0u))
#define AFG_CRIT_TIMER_M1 (MARF_PULSE_HAS_EXTI2_15 \
                           ? (1u << (EXTI15_10_IRQn - 32)) : 0u)
#define AFG_CRIT_THREAD_M0 (AFG_CRIT_TIMER_M0 | (1u << EXTI0_IRQn) \
                            | (1u << EXTI1_IRQn) | (1u << TIM4_IRQn))
#define AFG_CRIT_THREAD_M1 (AFG_CRIT_TIMER_M1 | (1u << (TIM5_IRQn - 32)))

// When the bus is not compiled in there is nothing to protect from a global
// disable, so keep the original __disable_irq() behaviour byte for byte. This
// change exists to serve the bus slave; the default firmware should not pay
// for it, nor carry an untested interrupt-priority change.
typedef struct { uint32_t w0, w1; } AfgCritState;

#if !BUS200E_ENABLE

static inline AfgCritState afg_crit_enter(uint32_t m0, uint32_t m1) {
  AfgCritState st = { 0u, 0u };
  (void) m0; (void) m1;
  __disable_irq();
  return st;
}
static inline void afg_crit_exit(AfgCritState st) { (void) st; __enable_irq(); }

#else

static inline AfgCritState afg_crit_enter(uint32_t m0, uint32_t m1) {
  AfgCritState st;
  // Save what was enabled rather than re-enabling blindly on exit:
  // controller.c's adc_pause() disables ADC_IRQn for stretches at a time, and
  // an unconditional re-enable would silently undo it.
  st.w0 = NVIC->ISER[0] & m0;
  st.w1 = NVIC->ISER[1] & m1;
  NVIC->ICER[0] = m0;
  if (m1) NVIC->ICER[1] = m1;
  __DSB();   // the disable must be in effect before the protected region
  __ISB();
  return st;
}

static inline void afg_crit_exit(AfgCritState st) {
  NVIC->ISER[0] = st.w0;          // writing a 0 bit is a no-op
  if (st.w1) NVIC->ISER[1] = st.w1;
}

#endif  // BUS200E_ENABLE

#define AFG_CRIT_TIMER_ENTER()  afg_crit_enter(AFG_CRIT_TIMER_M0, AFG_CRIT_TIMER_M1)
#define AFG_CRIT_THREAD_ENTER() afg_crit_enter(AFG_CRIT_THREAD_M0, AFG_CRIT_THREAD_M1)

#endif
