/*
 * Minimal host shim for <stm32f4xx.h>.
 *
 * Just enough of the ST device header for the MARF's pure logic
 * (program.c, analog_data.c, scales.c, ...) to compile and run on a
 * development host for unit testing. NOT for building real firmware.
 *
 * On target the real ST header is used; the host test build puts this
 * directory first on the include path instead.
 */
#ifndef SHIM_STM32F4XX_H
#define SHIM_STM32F4XX_H

#include <stdint.h>

typedef enum { RESET = 0, SET = !RESET } FlagStatus, ITStatus;
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;

/* ---- GPIO ---------------------------------------------------------------- */
typedef struct {
  volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRRL, BSRRH, LCKR, AFR[2];
} GPIO_TypeDef;

extern GPIO_TypeDef _shim_gpio;   /* defined in test_support.c */
#define GPIOA (&_shim_gpio)
#define GPIOB (&_shim_gpio)
#define GPIOC (&_shim_gpio)
#define GPIOD (&_shim_gpio)

#define GPIO_Pin_0  (1u << 0)
#define GPIO_Pin_1  (1u << 1)
#define GPIO_Pin_2  (1u << 2)
#define GPIO_Pin_3  (1u << 3)
#define GPIO_Pin_4  (1u << 4)
#define GPIO_Pin_5  (1u << 5)
#define GPIO_Pin_6  (1u << 6)
#define GPIO_Pin_7  (1u << 7)
#define GPIO_Pin_8  (1u << 8)
#define GPIO_Pin_9  (1u << 9)
#define GPIO_Pin_10 (1u << 10)
#define GPIO_Pin_11 (1u << 11)
#define GPIO_Pin_12 (1u << 12)
#define GPIO_Pin_13 (1u << 13)
#define GPIO_Pin_14 (1u << 14)
#define GPIO_Pin_15 (1u << 15)

/* ---- EXTI ---------------------------------------------------------------- */
#define EXTI_Line0  (1u << 0)
#define EXTI_Line1  (1u << 1)
#define EXTI_Line2  (1u << 2)
#define EXTI_Line5  (1u << 5)
#define EXTI_Line6  (1u << 6)
#define EXTI_Line7  (1u << 7)
#define EXTI_Line8  (1u << 8)
#define EXTI_Line14 (1u << 14)

static inline FlagStatus EXTI_GetFlagStatus(uint32_t line) { (void) line; return RESET; }

/* ---- TIM (enough for afg.c's start-signal timers) ------------------------ */
typedef struct {
  volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER,
                    CNT, PSC, ARR, RCR, CCR1, CCR2, CCR3, CCR4, BDTR, DCR, DMAR;
} TIM_TypeDef;

extern TIM_TypeDef _shim_tim3, _shim_tim7;   /* defined in test_support.c */
#define TIM3 (&_shim_tim3)
#define TIM7 (&_shim_tim7)

#define TIM_CR1_CEN   (1u << 0)
#define TIM_DIER_UIE  (1u << 0)

/* ---- NVIC / RCC (no-ops on host) ----------------------------------------- */
typedef enum { TIM3_IRQn = 29, TIM7_IRQn = 55 } IRQn_Type;
static inline void NVIC_EnableIRQ(IRQn_Type irq) { (void) irq; }

#define RCC_APB1Periph_TIM3 (1u << 1)
#define RCC_APB1Periph_TIM7 (1u << 5)
static inline void RCC_APB1PeriphClockCmd(uint32_t periph, FunctionalState st) {
  (void) periph; (void) st;
}

#define RCC_AHB1Periph_GPIOC (1u << 2)
static inline void RCC_AHB1PeriphClockCmd(uint32_t periph, FunctionalState st) {
  (void) periph; (void) st;
}

/* -std=c11 hides the `asm` keyword; the firmware's nop-delay macros use it. */
#ifndef asm
#define asm __asm__
#endif

#endif /* SHIM_STM32F4XX_H */
