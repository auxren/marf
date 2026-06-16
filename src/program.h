#ifndef __PROGRAM_H
#define __PROGRAM_H

#include <stdbool.h>
#include <stm32f4xx.h>

#include "data_types.h"
#include "expander.h"
#include "analog_data.h"
#include "HC165.h"
#include "constants.h"

// Main structure for step data type
typedef union
{
  struct {
    unsigned int Quantize:1;
    unsigned int Sloped:1;
    unsigned int FullRange:1;
    unsigned int VoltageSource:1;
    unsigned int Voltage0:1;
    unsigned int Voltage2:1;
    unsigned int Voltage4:1;
    unsigned int Voltage6:1;
    unsigned int Voltage8:1;
    unsigned int OpModeSTOP:1;
    unsigned int OpModeSUSTAIN:1;
    unsigned int OpModeENABLE:1;
    unsigned int CycleFirst:1;
    unsigned int CycleLast:1;
    unsigned int TimeRange_p03:1;
    unsigned int TimeRange_p3:1;
    unsigned int TimeRange_3:1;
    unsigned int TimeRange_30:1;
    unsigned int TimeSource:1;
    unsigned int OutputPulse1:1;
    unsigned int OutputPulse2:1;
    unsigned int TuringClock:2;   // which external jack (0-3) clocks this stage's register
    unsigned int TuringLength:4;  // shift register length, stored as length-2 (0..14 -> 2..16)
    unsigned int PulseWidth:4;    // per-step pulse/gate length: 0 (~1% of the step) .. 15 (~99%)
  } b;
  unsigned char val[4];
} uStep;

// Slider positions
typedef struct {
    uint16_t VLevel;
    uint16_t TLevel;
} StepSliders;

// Book keeping for which sliders are pinned after loading a stored program.
// This is kept little endian (so LSB is slider 0 and MSB is slider 31).
// High bit means the slider reading is higher than the pin.
// Low bit means the slider reading is lower than the pin value.
// When both high and low bits are unset then the slider is un-pinned.
typedef struct {
  uint32_t high;
  uint32_t low;
} PinnedSliders;

// Afg mode and run state for coordination between controller and display
typedef struct {
  // Run mode, strictly one of the defs above
  uint8_t mode;
  // The step section (normally 0 or 1 if shifted to 16-31)
  uint8_t section;
  // The current step number
  uint8_t step_num;
} AfgControllerState;


// Main steps and sliders array data
// This is extern visible so that we can inline fast access to it, but
// DO NOT ACCESS IT DIRECTLY from any other source file.
extern volatile uStep steps[32];

extern volatile StepSliders sliders[32];
extern volatile PinnedSliders voltage_slider_pins;
extern volatile PinnedSliders time_slider_pins;

// Live (raw) slider positions and the scale-select freeze flag (see program.c).
extern volatile uint16_t slider_raw_v[32];
extern volatile uint16_t slider_raw_t[32];
extern volatile uint8_t scale_select_freeze;

void InitProgram();

// Randomize one 16-step block (section 0 = stages 1-16, 1 = stages 17-32):
// slider values, voltage range, quantize/slope/pulses, time range, and a random
// loop length within the block. The other block (the other AFG) is left
// untouched. Caller should reseed the PRNG (turing_seed) first for variety.
void RandomizeProgram(uint8_t section);

static inline uint8_t get_max_step() {
  return Is_Expander_Present() ? 31 : 15;
}

// A 12bit value shifted into range for 32 or 16 step selection
static inline uint8_t get_max_step_shift12() {
  return Is_Expander_Present() ? 7 : 8;
}

static inline uStep get_step_programming(uint8_t section, uint8_t step_num) {
  step_num += section << 4;
  return steps[step_num];
}

void WriteVoltageSlider(uint8_t slider_num, uint32_t new_adc_reading);

void WriteTimeSlider(uint8_t slider_num, uint32_t new_adc_reading);

// Two-point slider calibration. ClearSliderCalibration() = passthrough (raw);
// SetSliderCalibration() applies captured per-slider min/max (offset + gain).
void ClearSliderCalibration(void);
void SetSliderCalibration(const uint16_t *v_min, const uint16_t *v_max,
                          const uint16_t *t_min, const uint16_t *t_max);

void WriteOtherCv(uint8_t cv_num, uint32_t new_adc_reading);

// Return the voltage for step number in section, quantized (when the step's
// Quantize bit is set) to the given scale/root. When use_override is set, an
// external-source step uses override_value (0..4095) instead of reading an
// external input -- used to feed in a Turing-machine value.
float GetStepVoltage(uint8_t section, uint8_t step_num, uint8_t scale, uint8_t root,
                     uint16_t override_value, uint8_t use_override);

// The time multiplier panel is marked for log scale (0.5, 1, 2, 4) but linear pots are used.

// Scale the time multipliers to more closely match the panel.
// Use a linear interpolation between the points instead of log2.

static inline float scale_time_fake_log2(float linear_val) {
  if (linear_val < 1365.0) {
    // 512 - 1024 or 0.5 - 1
    return linear_val * 0.375 + 512.0;
  } else if (linear_val < 2730) {
    // 1024 - 2048 or 1 - 2
    return (linear_val - 1365) * 0.882 + 1024.0;
  } else {
    // 2048 - 4095 or 2 - 4
    return (linear_val - 2730) * 1.5 + 2048.0;
  }
}

// v1-only: piecewise-linear approximation of a log2 taper applied to the time
// FADER (slider), matching the original v1.6 firmware so the v1 step rate feels
// the same. (v2 uses the raw linear slider level.)
static inline float scale_time_fader_fake_log2(float linear_val) {
  if (linear_val < 910)
    return linear_val * 0.160;
  else if (linear_val < 1457)
    return linear_val * 0.5378 - 344.44;
  else if (linear_val < 2185)
    return linear_val * 1.0 - 1023.91;
  else if (linear_val < 3004)
    return linear_val * 1.786 - 2731.94;
  else
    return linear_val * 1.339 - 1389.7;
}

// Get step time slider level as simple linear value
static inline uint16_t get_time_slider_level(uint8_t slider_num) {
  return sliders[slider_num].TLevel;
}

uint32_t GetStepWidth(uint8_t section, uint8_t step_num, float time_multiplier);

uint8_t GetNextStep(uint8_t section, uint8_t step_num);

void ApplyProgrammingSwitches(uint8_t section, uint8_t step_num, uButtons *switches);

void ClearProgram(uint8_t section);

static inline void pin_all_sliders() {
  voltage_slider_pins.high = 0xFFFFFFFF;
  voltage_slider_pins.low = 0xFFFFFFFF;
  time_slider_pins.high = 0xFFFFFFFF;
  time_slider_pins.low = 0xFFFFFFFF;
}

static inline void unpin_all_sliders() {
  voltage_slider_pins.high = 0x00000000;
  voltage_slider_pins.low = 0x00000000;
  time_slider_pins.high = 0x00000000;
  time_slider_pins.low = 0x00000000;
}

#endif
