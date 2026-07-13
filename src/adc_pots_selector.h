#ifndef __ADC_POTS_SELECTOR_H_
#define __ADC_POTS_SELECTOR_H_

#include <stdint.h>
#include "marf_version.h"

void AdcMuxGpioInitialize(void);

void AdcMuxResetAllOff(void);

// Reset to pot 1 selected
uint8_t AdcMuxReset(void);

// Advances the mux to the next pot and returns its index
uint8_t AdcMuxAdvance(uint8_t pot);
uint8_t AdcMuxAdvanceExpanded(uint8_t pot);

// v1 only: tell the scanner which external input (pot 16-19) is serving as a
// shift-register clock so it can be oversampled; -1 = none. No-op on v2.
#if MARF_HW == 1
void AdcMuxSetHotPot(int8_t pot);
#else
#define AdcMuxSetHotPot(pot) ((void)0)
#endif

// Explicitly select one of the adc2 channels
void AdcMuxSelectAdc2(uint8_t pot);


#endif
