
#ifndef __CYCLE_COUNTER_H
#define __CYCLE_COUNTER_H

// Core debug and DWT registers (Cortex-M4)
#define CYCLE_COUNTER_DEMCR    (*((volatile uint32_t*)0xE000EDFC)) // DEMCR
#define CYCLE_COUNTER_DWT_CTRL (*((volatile uint32_t*)0xE0001000)) // DWT_CTRL
#define CYCLE_COUNTER_DWT_CYC  (*((volatile uint32_t*)0xE0001004)) // DWT_CYCCNT

// Gets the current processor cycle count register
#define CLOCK_SOURCE_GET_TIMER() (CYCLE_COUNTER_DWT_CYC)

static inline void start_cycle_timer() {
    // The DWT cycle counter only runs when the trace subsystem is enabled,
    // so set DEMCR.TRCENA (bit 24) first; otherwise CYCCNT stays at 0.
    CYCLE_COUNTER_DEMCR |= 0x01000000;
    // Reset and enable the cycle counter (DWT_CTRL.CYCCNTENA, bit 0)
    CYCLE_COUNTER_DWT_CYC = 0;
    CYCLE_COUNTER_DWT_CTRL |= 0x00000001;
  }

#endif
