#ifndef __MARF_VERSION_H
#define __MARF_VERSION_H

#include <stm32f4xx.h>

// ---------------------------------------------------------------------------
// Target hardware revision.
//
//   MARF_HW = 2  (default) : SAModular / EMS "v2" board.
//   MARF_HW = 1            : original "v1" board (e.g. v1.x / v1.6).
//
// Both boards use an STM32F405RG with the same DACs (internal + MAX5135) and
// the same EEPROM / shift-register / ADC-mux wiring. They differ in:
//   * the DIP-switch GPIO pins (and which switch maps to which function), and
//   * the pulse-input wiring: on v1, START is NOT connected to an interrupt
//     pin, so it cannot be used without a hardware mod. MARF_V1_NO_STROBE
//     (default on v1) drops the START inputs accordingly.
//
// The v1 pin map here is reverse-engineered from the original v1 firmware
// (wir35/marf @ v1.0) and is UNVERIFIED on real v1 hardware -- treat a v1 build
// as a candidate to be confirmed by someone with a v1 module.
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

  // Pulse inputs wired on v1 (GPIOB): stop1/stop2/strobe1/strobe2 only.
  // START (PB8/PB6) is not on an interrupt pin -> no-strobe build.
  #ifndef MARF_V1_NO_STROBE
  #define MARF_V1_NO_STROBE 1
  #endif
  #define MARF_PULSE_GPIO_PINS     (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_5 | GPIO_Pin_7)
  #define MARF_PULSE_HAS_START     0

#else

  // -- v2 board (default) -------------------------------------------------
  // DIP switches (GPIOA)
  #define MARF_DIP_PINS            (GPIO_Pin_8 | GPIO_Pin_11 | GPIO_Pin_15)
  #define MARF_DIP_V_OUT_1V2_PIN   GPIO_Pin_11
  #define MARF_DIP_V_OUT_1V_PIN    GPIO_Pin_15
  #define MARF_DIP_EXPANDER_PIN    GPIO_Pin_8
  #define MARF_DIP_HAS_SAVE_PIN    0

  // All six pulse inputs wired on v2.
  #define MARF_PULSE_GPIO_PINS     (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8)
  #define MARF_PULSE_HAS_START     1

#endif

#endif
