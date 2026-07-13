#include "adc_pots_selector.h"

#include <stm32f4xx_gpio.h>
#include <stdlib.h>
#include <string.h> // memset

#include "delays.h"
#include "marf_version.h"

#define ADC_PS_SH_PIN	GPIO_Pin_13
#define ADC_PS_DS_PIN	GPIO_Pin_14
#define ADC_PS_ST_PIN	GPIO_Pin_15

#define ADC_POTS_SELECTOR_SHIFT_HIGH		GPIOC->BSRRL = ADC_PS_SH_PIN
#define ADC_POTS_SELECTOR_SHIFT_LOW			GPIOC->BSRRH = ADC_PS_SH_PIN

#define ADC_POTS_SELECTOR_STORAGE_HIGH	GPIOC->BSRRL = ADC_PS_ST_PIN
#define ADC_POTS_SELECTOR_STORAGE_LOW		GPIOC->BSRRH = ADC_PS_ST_PIN

#define ADC_POTS_SELECTOR_DATA_HIGH			GPIOC->BSRRL = ADC_PS_DS_PIN
#define ADC_POTS_SELECTOR_DATA_LOW			GPIOC->BSRRH = ADC_PS_DS_PIN

// Data for ADC channels selection
// No longer used, but a useful reference.

#if 0
const uint64_t adc_mux_channel_select_data[72] = {
	0xFFFFFFF0, //Time1 CH
	0xFFFFFFF1, //Time2 CH
	0xFFFFFFF2, //Time3 CH
	0xFFFFFFF3, //Time4 CH
	0xFFFFFFF4, //Time5 CH
	0xFFFFFFF5, //Time6 CH
	0xFFFFFFF6, //Time7 CH
	0xFFFFFFF7, //Time8 CH
	0xFFFFFF0F, //Time9 CH
	0xFFFFFF1F, //Time10 CH
	0xFFFFFF2F, //Time11 CH
	0xFFFFFF3F, //Time12 CH
	0xFFFFFF4F, //Time13 CH
	0xFFFFFF5F, //Time14 CH
	0xFFFFFF6F, //Time15 CH
	0xFFFFFF7F, //Time16 CH
	0xFFFFF0F0, //EXT INPUT A
	0xFFFFF1F1, //EXT INPUT B
	0xFFFFF2F2, //EXT INPUT C
	0xFFFFF3F3, //EXT INPUT D
	0xFFFFF4F4, //TIME MULT 1
	0xFFFFF5F5, //TIME MULT 2
	0xFFFFF6F6, //EXT STAGE 1
	0xFFFFF7F7, //EXT STAGE 2
	// Both are technically correct since the last byte is irrelevant to that mux
	/* 0xFFFFF0FF, //EXT INPUT A */
	/* 0xFFFFF1FF, //EXT INPUT B */
	/* 0xFFFFF2FF, //EXT INPUT C */
	/* 0xFFFFF3FF, //EXT INPUT D */
	/* 0xFFFFF4FF, //TIME MULT 1 */
	/* 0xFFFFF5FF, //TIME MULT 2 */
	/* 0xFFFFF6FF, //EXT STAGE 1 */
	/* 0xFFFFF7FF, //EXT STAGE 2 */
	0xFFF0FFFF, //Volt1 CH
	0xFFF1FFFF, //Volt2 CH
	0xFFF2FFFF, //Volt3 CH
	0xFFF3FFFF, //Volt4 CH
	0xFFF4FFFF, //Volt5 CH
	0xFFF5FFFF, //Volt6 CH
	0xFFF6FFFF, //Volt7 CH
	0xFFF7FFFF, //Volt8 CH
	0xFF0FFFFF, //Volt9 CH
	0xFF1FFFFF, //Volt10 CH
	0xFF2FFFFF, //Volt11 CH
	0xFF3FFFFF, //Volt12 CH
	0xFF4FFFFF, //Volt13 CH
	0xFF5FFFFF, //Volt14 CH
	0xFF6FFFFF, //Volt15 CH
	0xFF7FFFFF, //Volt16 CH
	0xF0FFFFFF,
	0xF1FFFFFF,
	0xF2FFFFFF,
	0xF3FFFFFF,
	0xF4FFFFFF,
	0xF5FFFFFF,
	0xF6FFFFFF,
	0xF7FFFFFF,
	0x0FFFFFFF,
	0x1FFFFFFF,
	0x2FFFFFFF,
	0x3FFFFFFF,
	0x4FFFFFFF,
	0x5FFFFFFF,
	0x6FFFFFFF,
	0x7FFFFFFF,
	0x0FFFFFFFF,
	0x1FFFFFFFF,
	0x2FFFFFFFF,
	0x3FFFFFFFF,
	0x4FFFFFFFF,
	0x5FFFFFFFF,
	0x6FFFFFFFF,
	0x7FFFFFFFF,
	0x0FFFFFFFFF,
	0x1FFFFFFFFF,
	0x2FFFFFFFFF,
	0x3FFFFFFFFF,
	0x4FFFFFFFFF,
	0x5FFFFFFFFF,
	0x6FFFFFFFFF,
	0x7FFFFFFFFF
	
	//0xFFFFFFFF	//ALL CHANNELS OFF
};
#endif


// Send one byte to the shift registers that drive the ADC multiplexers
inline static void adc_mux_send_byte(const uint8_t data) {
  uint8_t dat = data;

  for (uint8_t cnt = 0; cnt < 8; cnt++) {
    if ((dat & 0x80) > 0) {
      ADC_POTS_SELECTOR_DATA_HIGH;
    } else {
      ADC_POTS_SELECTOR_DATA_LOW;
    }
    ADC_POTS_SELECTOR_SHIFT_LOW;
    DELAY_NOPS_120NS();
    ADC_POTS_SELECTOR_SHIFT_HIGH;
    DELAY_NOPS_120NS();
    dat = dat << 1;
  }
  ADC_POTS_SELECTOR_DATA_LOW;
}

// Send one half byte to the shift registers that drive the ADC multiplexers
inline static void adc_mux_send_nibble(const uint8_t data) {
  uint8_t dat = data;

  for(uint8_t cnt = 0; cnt < 4; cnt++) {
    if ((dat & 0x8) > 0) {
      ADC_POTS_SELECTOR_DATA_HIGH;
    } else {
      ADC_POTS_SELECTOR_DATA_LOW;
    }
    ADC_POTS_SELECTOR_SHIFT_LOW;
    DELAY_NOPS_120NS();
    ADC_POTS_SELECTOR_SHIFT_HIGH;
    DELAY_NOPS_120NS();
    dat = dat << 1;
  }
  ADC_POTS_SELECTOR_DATA_LOW;
}

// Send five bytes to the shift registers that drive the ADC multiplexers.
// This updates all five shift registers in an expanded module.
inline static void adc_mux_send_word(const unsigned long long int data) {
  ADC_POTS_SELECTOR_STORAGE_LOW;
  adc_mux_send_byte((uint8_t) ((data&0xFF00000000) >>32));
  adc_mux_send_byte((uint8_t) ((data&0x00FF000000) >>24));
  adc_mux_send_byte((uint8_t) ( data&0x00000000FF));
  adc_mux_send_byte((uint8_t) ((data&0x000000FF00) >>8));
  adc_mux_send_byte((uint8_t) ((data&0x0000FF0000) >>16));
  ADC_POTS_SELECTOR_STORAGE_HIGH;
}

#if MARF_HW == 1

// ---------------------------------------------------------------------------
// v1 boards: explicit full-chain channel selection.
//
// The optimized partial-shift scheme below (v2) assumes the exact v2 mux
// shift-register chain. On real v1 hardware it does not land selections where
// it thinks (proven 2026-07-12: every analog channel frozen under the partial
// shifts, all working under the stock v1.0 firmware on the same unit). The
// original v1.0 firmware (build 207) instead writes the ENTIRE 5-byte chain
// state on every selection - correct by construction on any chain revision.
// This is that table and strategy, verbatim; the send byte order and latch
// behaviour of adc_mux_send_word() match v1.0's SendDWord exactly.
// ---------------------------------------------------------------------------

// Indexed DIRECTLY by our scan pot number: v1.0's reader used the same layout
// (0-15 voltage sliders via ADC1, 16-23 CV bank via ADC2, 24-39 time sliders
// via ADC1, 40-71 expander). NOTE: the bank comments in the original v1.0
// table ("Time1 CH", "Volt1 CH") are mislabeled relative to its own reader;
// the comments below follow the reader (= the truth).
static const unsigned long long int v1_ch_sel_data[72] = {
  0xFFFFFFFFF0, 0xFFFFFFFFF1, 0xFFFFFFFFF2, 0xFFFFFFFFF3,  // Volt 1-4
  0xFFFFFFFFF4, 0xFFFFFFFFF5, 0xFFFFFFFFF6, 0xFFFFFFFFF7,  // Volt 5-8
  0xFFFFFFFF0F, 0xFFFFFFFF1F, 0xFFFFFFFF2F, 0xFFFFFFFF3F,  // Volt 9-12
  0xFFFFFFFF4F, 0xFFFFFFFF5F, 0xFFFFFFFF6F, 0xFFFFFFFF7F,  // Volt 13-16
  0xFFFFFFF0FF, 0xFFFFFFF1FF, 0xFFFFFFF2FF, 0xFFFFFFF3FF,  // Ext input A-D
  0xFFFFFFF4FF, 0xFFFFFFF5FF, 0xFFFFFFF6FF, 0xFFFFFFF7FF,  // TM 1/2, Stage 1/2
  0xFFFFF0FFFF, 0xFFFFF1FFFF, 0xFFFFF2FFFF, 0xFFFFF3FFFF,  // Time 1-4
  0xFFFFF4FFFF, 0xFFFFF5FFFF, 0xFFFFF6FFFF, 0xFFFFF7FFFF,  // Time 5-8
  0xFFFF0FFFFF, 0xFFFF1FFFFF, 0xFFFF2FFFFF, 0xFFFF3FFFFF,  // Time 9-12
  0xFFFF4FFFFF, 0xFFFF5FFFFF, 0xFFFF6FFFFF, 0xFFFF7FFFFF,  // Time 13-16
  0xFFF0FFFFFF, 0xFFF1FFFFFF, 0xFFF2FFFFFF, 0xFFF3FFFFFF,  // Expander volt 1-4
  0xFFF4FFFFFF, 0xFFF5FFFFFF, 0xFFF6FFFFFF, 0xFFF7FFFFFF,  // Expander volt 5-8
  0xFF0FFFFFFF, 0xFF1FFFFFFF, 0xFF2FFFFFFF, 0xFF3FFFFFFF,  // Expander volt 9-12
  0xFF4FFFFFFF, 0xFF5FFFFFFF, 0xFF6FFFFFFF, 0xFF7FFFFFFF,  // Expander volt 13-16
  0xF0FFFFFFFF, 0xF1FFFFFFFF, 0xF2FFFFFFFF, 0xF3FFFFFFFF,  // Expander time 1-4
  0xF4FFFFFFFF, 0xF5FFFFFFFF, 0xF6FFFFFFFF, 0xF7FFFFFFFF,  // Expander time 5-8
  0x0FFFFFFFFF, 0x1FFFFFFFFF, 0x2FFFFFFFFF, 0x3FFFFFFFFF,  // Expander time 9-12
  0x4FFFFFFFFF, 0x5FFFFFFFFF, 0x6FFFFFFFFF, 0x7FFFFFFFFF   // Expander time 13-16
};

static inline void v1_select_pot(uint8_t pot) {
  adc_mux_send_word(v1_ch_sel_data[pot]);
}

// The external input currently serving as a shift-register clock (16-19), or
// -1 when Turing mode is off / nothing is being clocked. Set by the
// controller; oversampled by AdcMuxAdvance below.
static volatile int8_t v1_hot_pot = -1;

void AdcMuxSetHotPot(int8_t pot) {
  v1_hot_pot = pot;
}

#endif  // MARF_HW == 1

void AdcMuxResetAllOff() {
  adc_mux_send_word(0xFFFFFFFFFF);
}

uint8_t AdcMuxReset() {
#if MARF_HW == 1
  v1_select_pot(0);                 // full-chain select of voltage slider 1
#else
  adc_mux_send_word(0xFFFFFFFFF0);
#endif
  return 0;
}

// Init GPIOs for ADC channels multiplexers
void AdcMuxGpioInitialize(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	memset(&GPIO_InitStructure, 0, sizeof(GPIO_InitStructure));
	GPIO_InitStructure.GPIO_Pin 	= ADC_PS_SH_PIN|ADC_PS_ST_PIN|ADC_PS_DS_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd	= GPIO_PuPd_UP;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
}

// Select next ADC channel and return its index. For unexpanded modules.
// Muxes are addressed by three shift registers in series as follows:
//   1. time sliders aka pots 24-39
//   2. (first half) external voltages aka pots 16-23
//   2. (unused half a shift register)
//   3. voltage sliders  aka pots 0 - 15

uint8_t AdcMuxAdvance(uint8_t pot) {
#if MARF_HW == 1
  // v1: explicit full-chain masks make the scan order arbitrary. The ACTIVE
  // shift-register clock input (set by the controller, pots 16-19) is
  // OVERSAMPLED - visited every other slot (~0.3 ms) - because it is
  // edge-detected from these samples and musical clock pulses are far shorter
  // than a full scan lap. Everything else walks the normal 0-39 sequence in
  // the remaining slots.
  static uint8_t v1_base = 39;
  static uint8_t v1_phase = 0;
  uint8_t next_pot;
  (void) pot;
  v1_phase ^= 1;
  if (v1_phase && v1_hot_pot >= 16 && v1_hot_pot <= 19) {
    next_pot = (uint8_t) v1_hot_pot;
  } else {
    v1_base = (uint8_t) ((v1_base + 1) % 40);
    next_pot = v1_base;
  }
  v1_select_pot(next_pot);
  DELAY_NOPS_120NS();
  return next_pot;
#else
  uint8_t next_pot, channel;

  // Here is a fancy hack.
  // The lowest three bits of the pot number (0-7) tell which channels of the mux (A,B,C) are live
  channel = pot & 0x7;

  ADC_POTS_SELECTOR_STORAGE_LOW;

  // Here is an even fancier hack. This addressing scheme requires shifting at most 1 byte every change, rather than 3.
  if (pot >= 8 && pot < 16) {
    // On the final set of time sliders
    channel = (channel + 1) & 0x7;   // increment the channel and wrap
    adc_mux_send_nibble(channel);    //
    next_pot = 24 + channel;         // time slider 1-8 are pots 24 - 31.
  }
  else if (pot >=16 && pot < 24) {
    // We're on the external mux
    adc_mux_send_byte(0xFF);        // Need to shift twice to get to the voltage slider
    next_pot = channel;             // voltage sliders are pots 0-7
  }
  else {
    adc_mux_send_nibble(0xF);       // just shift to next mux
    if (pot >= 32 && pot < 40) {
      // Final time sliders, external voltages are next
      next_pot = pot - 16;
    } else {
      next_pot = pot + 8;           // shift just moves pot count along by 8
    }
  }
  // Activate the shift registers with the new data
  ADC_POTS_SELECTOR_STORAGE_HIGH;
  DELAY_NOPS_120NS();
  return next_pot;
#endif
}

// Select next ADC channel and return its index. For expanded modules.
// The logic is the same as above with an additional two more registers.
// Muxes are addressed by five shift registers in series as follows:
//   1. time sliders aka pots 24-39
//   2. (first half) external voltages aka pots 16-23
//   2. (unused half a shift register)
//   3. voltage sliders  aka pots 0 - 15
//   4. expander voltage sliders aka pots 40 - 55
//   5. expander time sliders aka pots 56 - 71

uint8_t AdcMuxAdvanceExpanded(uint8_t pot) {
#if MARF_HW == 1
  uint8_t next_pot = (uint8_t) ((pot + 1) % 72);
  v1_select_pot(next_pot);
  DELAY_NOPS_120NS();
  return next_pot;
#else
  uint8_t next_pot, channel;

  channel = pot & 0x7;
  if (pot >= 64 && pot < 72) {
    // Final register
    channel = (channel + 1) & 0x7;
    adc_mux_send_nibble(channel);
    next_pot = 24 + channel;  // time slider 1-8 are pots 24 - 31.
  }
  else if (pot >=16 && pot < 24) {
    // Second (external) register
    adc_mux_send_byte(0xFF);
    next_pot = 0 + channel; // voltage sliders are pots 0-7
  }
  else {
    adc_mux_send_nibble(0xF); // just shift to next mux
    if (pot >= 32 && pot < 40) {
      // final time sliders, external voltages are next
      next_pot = pot - 16;
    } else if (pot >=8 && pot < 16) {
      // from mux 3 to mux 4
      next_pot = pot + 32; 
    } else  {
      next_pot = pot + 8; // otherwise shift 8
    }
  }
  // activate the shift register with the new data
  ADC_POTS_SELECTOR_STORAGE_LOW;
  DELAY_CLOCK_20();  // Slightly longer settling time
  ADC_POTS_SELECTOR_STORAGE_HIGH;
  return next_pot;
#endif
}

// Select one of the adc2 channels explicitly (0-7)
void AdcMuxSelectAdc2(uint8_t pot) {
#if MARF_HW == 1
  adc_mux_send_word(v1_ch_sel_data[16 + (pot & 0x7)]);   // full-chain select
  DELAY_NOPS_120NS();
#else
  ADC_POTS_SELECTOR_STORAGE_LOW;

  adc_mux_send_nibble(0xF); // Unused nibble
  adc_mux_send_nibble(pot); // Select the channel
  adc_mux_send_byte(0xFF);  // Shift

  // Activate the shift registers with the new data
  ADC_POTS_SELECTOR_STORAGE_HIGH;
  DELAY_NOPS_120NS();
#endif
}
