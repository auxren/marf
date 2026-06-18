#ifndef __MARF_VERSION_H
#define __MARF_VERSION_H

#include <stm32f4xx.h>

// ---------------------------------------------------------------------------
// Target hardware revision.
//
//   MARF_HW = 2  (default) : SAModular / EMS "v2" board.
//   MARF_HW = 1            : original "v1" board (e.g. v1.x / v1.6).
//
// Both boards use an STM32F405RG with the same EEPROM / shift-register / ADC-mux
// wiring. They differ in:
//   * the MAX5135 Time/Ref DAC: v1 is 12-bit (3-byte frame, CPOL high, needs a
//     power/clear/linearity init); v2 is 10-bit (2-byte frame, CPOL low). Gated
//     on MARF_HW in MAX5135.c and afg.c (ported from the v1.6 source).
//     (The v1.6 log2 time-fader taper is deliberately NOT used -- both boards
//     use the linear fader so they run at the same rate; see program.c.)
//   * the DIP-switch GPIO pins (and which switch maps to which function), and
//   * the pulse-input wiring (GPIOB):
//       v2: STOP1=PB0 STOP2=PB1 STROBE1=PB5 STROBE2=PB7 START1=PB8 START2=PB6
//       v1: STOP1=PB0 STOP2=PB1 START1=PB7 START2=PB5 (NO strobe input)
//     i.e. on v1 the START signals are on PB7/PB5 and there is no strobe jack
//     (strobe is a panel-only function). This is the "no-strobe" board.
//
// The v1 pin map matches the original v1.0 firmware source (B248 rev 1.0).
// ---------------------------------------------------------------------------

#ifndef MARF_HW
#define MARF_HW 2
#endif

#if MARF_HW == 1

  // -- v1 board ----------------------------------------------------------
  // DIP switches (GPIOA)
  #define MARF_DIP_PINS            (GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11)
  #define MARF_DIP_V_OUT_1V2_PIN   GPIO_Pin_10
  #define MARF_DIP_V_OUT_1V_PIN    GPIO_Pin_9
  #define MARF_DIP_SAVE_PIN        GPIO_Pin_11
  #define MARF_DIP_EXPANDER_PIN    GPIO_Pin_8
  #define MARF_DIP_HAS_SAVE_PIN    1

  // Pulse inputs wired on v1 (GPIOB), straight from the MARF_v1.6 source
  // (analog_data.h get_afg*_pulse_inputs + main.c mInterruptInit):
  //   AFG1: START 1 = PB7, STOP 1 = PB0, STROBE 1 = PB2
  //   AFG2: START 2 = PB5, STOP 2 = PB1, STROBE 2 = PB14
  // v1 strobes are on PB2 (EXTI2) and PB14 (EXTI15_10), and the EXTI trigger is
  // RISING + FALLING (the v1 hardware needs both edges). v2 uses different pins
  // and rising-only. (The earlier "v1 = no-strobe, PB0/1/5/7" map was a
  // reverse-engineered guess and broke start/run on real v1 hardware.)
  #define MARF_PULSE_HAS_START     1
  #define MARF_PULSE_HAS_STROBE    1
  #define MARF_PULSE_HAS_EXTI2_15  1   /* v1 strobes need EXTI2 + EXTI15_10 IRQs */
  #define MARF_PULSE_EXTI_TRIGGER  EXTI_Trigger_Rising_Falling
  #define MARF_PULSE_GPIO_PINS     (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_14)
  #define MARF_PULSE_EXTI_LINES    (EXTI_Line0 | EXTI_Line1 | EXTI_Line2 | EXTI_Line5 | EXTI_Line7 | EXTI_Line14)
  #define MARF_GPIO_STOP1    GPIO_Pin_0
  #define MARF_GPIO_STOP2    GPIO_Pin_1
  #define MARF_GPIO_START1   GPIO_Pin_7
  #define MARF_GPIO_START2   GPIO_Pin_5
  #define MARF_GPIO_STROBE1  GPIO_Pin_2
  #define MARF_GPIO_STROBE2  GPIO_Pin_14
  #define MARF_EXTI_STOP1    EXTI_Line0
  #define MARF_EXTI_STOP2    EXTI_Line1
  #define MARF_EXTI_START1   EXTI_Line7
  #define MARF_EXTI_START2   EXTI_Line5
  #define MARF_EXTI_STROBE1  EXTI_Line2
  #define MARF_EXTI_STROBE2  EXTI_Line14
  #define MARF_PULSE_SYSCFG_INIT() do { \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource0); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource1); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource2); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource5); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource7); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource14); \
    } while (0)

#else

  // -- v2 board (default) -------------------------------------------------
  // DIP switches (GPIOA)
  #define MARF_DIP_PINS            (GPIO_Pin_8 | GPIO_Pin_11 | GPIO_Pin_15)
  #define MARF_DIP_V_OUT_1V2_PIN   GPIO_Pin_11
  #define MARF_DIP_V_OUT_1V_PIN    GPIO_Pin_15
  #define MARF_DIP_EXPANDER_PIN    GPIO_Pin_8
  #define MARF_DIP_HAS_SAVE_PIN    0

  // All six pulse inputs wired on v2 (GPIOB):
  //   STOP 1 = PB0, STOP 2 = PB1, STROBE 1 = PB5, STROBE 2 = PB7,
  //   START 1 = PB8, START 2 = PB6.
  #define MARF_PULSE_HAS_START     1
  #define MARF_PULSE_HAS_STROBE    1
  #define MARF_PULSE_HAS_EXTI2_15  0   /* v2 pulse inputs all fall under EXTI0/1/9_5 */
  #define MARF_PULSE_EXTI_TRIGGER  EXTI_Trigger_Rising
  #define MARF_PULSE_GPIO_PINS     (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8)
  #define MARF_PULSE_EXTI_LINES    (EXTI_Line0 | EXTI_Line1 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 | EXTI_Line8)
  #define MARF_GPIO_STOP1    GPIO_Pin_0
  #define MARF_GPIO_STOP2    GPIO_Pin_1
  #define MARF_GPIO_START1   GPIO_Pin_8
  #define MARF_GPIO_START2   GPIO_Pin_6
  #define MARF_GPIO_STROBE1  GPIO_Pin_5
  #define MARF_GPIO_STROBE2  GPIO_Pin_7
  #define MARF_EXTI_STOP1    EXTI_Line0
  #define MARF_EXTI_STOP2    EXTI_Line1
  #define MARF_EXTI_START1   EXTI_Line8
  #define MARF_EXTI_START2   EXTI_Line6
  #define MARF_EXTI_STROBE1  EXTI_Line5
  #define MARF_EXTI_STROBE2  EXTI_Line7
  // Order matches the original v2 init (0,1,5,7,6,8) so the v2 binary is
  // unchanged; the order is irrelevant (each call configures an independent
  // EXTI source).
  #define MARF_PULSE_SYSCFG_INIT() do { \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource0); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource1); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource5); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource7); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource6); \
      SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, GPIO_PinSource8); \
    } while (0)

#endif

#endif
