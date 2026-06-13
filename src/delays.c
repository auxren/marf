#include "delays.h"

#include <stm32f4xx.h>

#include "cycle_counter.h"

// Systick
volatile uint32_t millis;

// Core clock in Hz. The system runs at 168 MHz (see system_stm32f4xx.c).
#define HCLK_HZ 168000000u

void systickInit(uint16_t frequency) {
  RCC_ClocksTypeDef RCC_Clocks;
  RCC_GetClocksFreq(&RCC_Clocks);
  (void) SysTick_Config(RCC_Clocks.HCLK_Frequency / frequency);
}

void SysTick_Handler (void) {
  millis++;
}

// Busy-wait for the given number of CPU cycles using the DWT cycle counter
// (enabled by start_cycle_timer() at boot). The unsigned subtraction is
// wrap-safe, and this works correctly from interrupt context.
static inline void delay_cycles(uint32_t cycles) {
  uint32_t start = CLOCK_SOURCE_GET_TIMER();
  while ((CLOCK_SOURCE_GET_TIMER() - start) < cycles) { }
}

void delay_ms(unsigned int ms)
{
  delay_cycles(ms * (HCLK_HZ / 1000u));
}

void delay_us(unsigned int us)
{
  delay_cycles(us * (HCLK_HZ / 1000000u));
}

void delay_ns(unsigned int ns)
{
  // 0.168 cycles/ns at 168 MHz. Round up so short waits are never zero.
  delay_cycles((ns * (HCLK_HZ / 1000000u) + 999u) / 1000u);
}
