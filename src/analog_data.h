#ifndef _ANALOG_DATA_H
#define _ANALOG_DATA_H

#include <stm32f4xx.h>
#include "dip_config.h"
#include "marf_version.h"

#define ADC_EXT_VOLTAGE_A     0x00
#define ADC_EXT_VOLTAGE_B     0x01
#define ADC_EXT_VOLTAGE_C     0x02
#define ADC_EXT_VOLTAGE_D     0x03
#define ADC_TIMEMULTIPLY_Ch_1 0x04
#define ADC_TIMEMULTIPLY_Ch_2 0x05
#define ADC_STAGEADDRESS_Ch_1 0x06
#define ADC_STAGEADDRESS_Ch_2 0x07

// Values of pulse inputs
typedef struct {
  uint8_t start;
  uint8_t stop;
  uint8_t strobe;
} PulseInputs;

extern PulseInputs PULSE_INPUTS_NONE;

static inline uint8_t any_pulses_high(PulseInputs in) {
  return in.start || in.stop || in.strobe;
}

// Additional analog data exposed globally for now

extern volatile uint16_t add_data[8];
extern volatile uint16_t cal_constants[8];

// CV-presence sensing on the four external inputs (A-D). external_present[k]
// latches high when a voltage appears on input k and clears after it stays idle.
// Used to soft-normal external-source steps: a step reads its external input
// when a CV is present, and falls back to its own slider value when nothing is
// patched there.
extern volatile uint8_t external_present[4];

// Update external_present[] from the latest readings. Call from the main loop.
void SenseExternalInputs(void);

// Calibration scalers for external inputs, precomputed in setup
extern float external_cal[8];

// Two-point (offset + gain) correction: calibrated = (raw - off) * scale.
extern float external_off[8];
extern float external_scale[8];

// Upgrade the 8 input/knob channels to two-point cal from captured min/max.
void SetTwoPointInputCalibration(const uint16_t *adc2_min, const uint16_t *adc2_max);

// Precomputed magic numbers for voltage scaling
// In the context of 12 bit range / 0.0 - 4095.0

extern float limited_range_multiplier; // octaves per 10v range
extern float octave_offset; // span of 1 octave
extern float semitone_offset; // span of 1 semitone
extern float quantizer_magic; // reciprocal of semitone_offset

// Voltage smoothers are low pass filters that keep intermediate 16 bit state from smoothed 12 bit readings.
// The filtering increases as the readings converge mainly to reduce jitter noise.

// Applies the voltage smoother to the state var passed.
// The new_reading should already be shifted to a 16 bit value.
// The returned value is shifted back down to 12 bit range.
static inline uint16_t apply_voltage_smoother(uint16_t new_reading, volatile uint16_t *state) {
  register uint16_t delta;

  if (new_reading > *state) {
    delta = new_reading - *state;
  } else {
    delta = *state - new_reading;
  }
  if (delta < 128) {
    // Apply a lot of filtering when the reading is close
    *state += (new_reading - *state) >> 4;
  } else if (delta < 512) {
    // More filtering
    *state += (new_reading - *state) >> 3;
  } else if (delta < 1024) {
    // Less filtering
    *state += (new_reading - *state) >> 2;
  } else if (delta < 2048) {
    // Less filtering
    *state += (new_reading - *state) >> 1;
  } else {
    // No filtering
    *state = new_reading;
  }
  return *state >> 4;
}

static inline float read_calibrated_add_data_float(uint8_t d) {
  float v = ((float) add_data[d] - external_off[d]) * external_scale[d];
  if (v < 0.0f) v = 0.0f;
  if (v > 4095.0f) v = 4095.0f;
  return v;
}

static inline uint16_t read_calibrated_add_data_uint16(uint8_t d) {
  return (uint16_t) (read_calibrated_add_data_float(d) + 0.5f);
}

void WriteOtherCv(uint8_t cv_num, uint32_t new_adc_reading);

void WriteOtherCvWithoutSmoothing(uint8_t cv_num, uint32_t new_adc_reading);

void PrecomputeCalibration(void);

void SetVoltageRange(uDipConfig dip_config);

// EXTI line / GPIO pin assignments come from marf_version.h and differ per
// board revision (v1: START on PB7/PB5, no strobe; v2: STROBE on PB5/PB7,
// START on PB8/PB6).
#define EXTI_LINE_START1  MARF_EXTI_START1
#define EXTI_LINE_STOP1   MARF_EXTI_STOP1
#define EXTI_LINE_STROBE1 MARF_EXTI_STROBE1

#define EXTI_LINE_START2  MARF_EXTI_START2
#define EXTI_LINE_STOP2   MARF_EXTI_STOP2
#define EXTI_LINE_STROBE2 MARF_EXTI_STROBE2

// Return the interrupt flag status for each pulse
static inline PulseInputs get_afg1_pulse_interrupts() {
  PulseInputs pulse_inputs = {};
  pulse_inputs.start  = MARF_PULSE_HAS_START  ? (EXTI_GetFlagStatus(EXTI_LINE_START1)  == SET) : 0;
  pulse_inputs.stop   = EXTI_GetFlagStatus(EXTI_LINE_STOP1) == SET;
  pulse_inputs.strobe = MARF_PULSE_HAS_STROBE ? (EXTI_GetFlagStatus(EXTI_LINE_STROBE1) == SET) : 0;
  return pulse_inputs;
}

// Return the interrupt flag status for each pulse
static inline PulseInputs get_afg2_pulse_interrupts() {
  PulseInputs pulse_inputs = {};
  pulse_inputs.start  = MARF_PULSE_HAS_START  ? (EXTI_GetFlagStatus(EXTI_LINE_START2)  == SET) : 0;
  pulse_inputs.stop   = EXTI_GetFlagStatus(EXTI_LINE_STOP2) == SET;
  pulse_inputs.strobe = MARF_PULSE_HAS_STROBE ? (EXTI_GetFlagStatus(EXTI_LINE_STROBE2) == SET) : 0;
  return pulse_inputs;
}

// Return the current ACTIVE state of the pulse inputs direct from the gpio
// pins (1 = a pulse is present at the jack).
// v1's Start/Stop input conditioning INVERTS: the lines idle HIGH and a pulse
// at the jack pulls them LOW (measured on real v1 hardware - it is also why
// v1 uses both-edge EXTI). Strobes idle LOW and are read directly on both
// revisions. All level-based logic (Enable/Sustain holds, the both-edge event
// qualification) reads through these accessors, so the inversion lives here.
static inline PulseInputs get_afg1_pulse_inputs() {
  PulseInputs pulse_inputs = {};
#if MARF_HW == 1
  pulse_inputs.start  = (GPIOB->IDR & MARF_GPIO_START1) == 0;
  pulse_inputs.stop   = (GPIOB->IDR & MARF_GPIO_STOP1)  == 0;
#else
  pulse_inputs.start  = MARF_PULSE_HAS_START ? ((GPIOB->IDR & MARF_GPIO_START1) != 0) : 0;
  pulse_inputs.stop   = (GPIOB->IDR & MARF_GPIO_STOP1) != 0;
#endif
  pulse_inputs.strobe = MARF_PULSE_HAS_STROBE ? ((GPIOB->IDR & MARF_GPIO_STROBE1) != 0) : 0;
  return pulse_inputs;
}

static inline PulseInputs get_afg2_pulse_inputs() {
  PulseInputs pulse_inputs = {};
#if MARF_HW == 1
  pulse_inputs.start  = (GPIOB->IDR & MARF_GPIO_START2) == 0;
  pulse_inputs.stop   = (GPIOB->IDR & MARF_GPIO_STOP2)  == 0;
#else
  pulse_inputs.start  = MARF_PULSE_HAS_START ? ((GPIOB->IDR & MARF_GPIO_START2) != 0) : 0;
  pulse_inputs.stop   = (GPIOB->IDR & MARF_GPIO_STOP2) != 0;
#endif
  pulse_inputs.strobe = MARF_PULSE_HAS_STROBE ? ((GPIOB->IDR & MARF_GPIO_STROBE2) != 0) : 0;
  return pulse_inputs;
}

#endif
