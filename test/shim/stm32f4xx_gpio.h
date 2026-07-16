/*
 * Host shim for <stm32f4xx_gpio.h> (StdPeriph GPIO driver header).
 *
 * Just enough for adc_pots_selector.c to compile on the host: the mux
 * bit-bang writes land in the shim GPIO's BSRRL/BSRRH, which the tests can
 * observe (or ignore).
 */
#ifndef SHIM_STM32F4XX_GPIO_H
#define SHIM_STM32F4XX_GPIO_H

#include <stm32f4xx.h>

typedef enum { GPIO_Mode_IN = 0, GPIO_Mode_OUT = 1, GPIO_Mode_AF = 2, GPIO_Mode_AN = 3 } GPIOMode_TypeDef;
typedef enum { GPIO_OType_PP = 0, GPIO_OType_OD = 1 } GPIOOType_TypeDef;
typedef enum { GPIO_Speed_2MHz = 0, GPIO_Speed_25MHz, GPIO_Speed_50MHz, GPIO_Speed_100MHz } GPIOSpeed_TypeDef;
typedef enum { GPIO_PuPd_NOPULL = 0, GPIO_PuPd_UP, GPIO_PuPd_DOWN } GPIOPuPd_TypeDef;

typedef struct {
  uint32_t GPIO_Pin;
  GPIOMode_TypeDef GPIO_Mode;
  GPIOSpeed_TypeDef GPIO_Speed;
  GPIOOType_TypeDef GPIO_OType;
  GPIOPuPd_TypeDef GPIO_PuPd;
} GPIO_InitTypeDef;

static inline void GPIO_Init(GPIO_TypeDef *g, GPIO_InitTypeDef *init) { (void) g; (void) init; }
static inline void GPIO_StructInit(GPIO_InitTypeDef *init) { (void) init; }
static inline void GPIO_SetBits(GPIO_TypeDef *g, uint16_t pins)   { g->ODR |=  pins; }
static inline void GPIO_ResetBits(GPIO_TypeDef *g, uint16_t pins) { g->ODR &= (uint32_t) ~pins; }

#endif /* SHIM_STM32F4XX_GPIO_H */
