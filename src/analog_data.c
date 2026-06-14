#include "analog_data.h"

#include <stm32f4xx.h>
#include "dip_config.h"
#include "delays.h"

// CV-presence state for the four external inputs.
volatile uint8_t external_present[4] = { 0, 0, 0, 0 };
volatile uint8_t external_normal[2] = { 0, 0 };

#define EXTERNAL_PRESENT_THRESHOLD 100    // ADC counts (~0.25 V) above idle
#define EXTERNAL_PRESENT_TIMEOUT_MS 1500  // stay "present" through brief 0 V dips

void SenseExternalInputs(void) {
  static uint32_t last_active[4] = { 0, 0, 0, 0 };
  uint32_t now = get_millis();
  for (uint8_t k = 0; k < 4; k++) {
    if (add_data[k] > EXTERNAL_PRESENT_THRESHOLD) {
      external_present[k] = 1;
      last_active[k] = now;
    } else if (now - last_active[k] > EXTERNAL_PRESENT_TIMEOUT_MS) {
      external_present[k] = 0;
    }
  }
}

// Additional analog data
volatile uint16_t add_data[8];

// Stored calibration
volatile uint16_t cal_constants[8] = {0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF};

// Calibration scalers for external inputs, precomputed in setup
float external_cal[8] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

PulseInputs PULSE_INPUTS_NONE = { 0, 0, 0 };

// Precomputed magic numbers for voltage scaling
// In the context of 12 bit range / 0.0 - 4095.0

float limited_range_multiplier; // octaves per 10v range
float octave_offset; // span of 1 octave
float semitone_offset; // span of 1 semitone
float quantizer_magic; // reciprocal of semitone_offset

// Set magic numbers from dip switch state
void SetVoltageRange(uDipConfig dip_config) {
  if (dip_config.b.V_OUT_1V) {
    // 1v per octave, who dis?
    octave_offset = 409.5;
    semitone_offset = 34.125;
    quantizer_magic = 0.0293;
    limited_range_multiplier = 0.1;
  } else if (dip_config.b.V_OUT_1V2) {
    // 1.2v per octave, the one true way
    octave_offset = 491.4;
    semitone_offset = 40.95;
    quantizer_magic = 0.02442;
    limited_range_multiplier = 0.12;
  } else {
    // 2v per octave for the OG's
    octave_offset = 819.0;
    semitone_offset = 68.25;
    quantizer_magic = 0.01465;
    limited_range_multiplier = 0.2;
  }
}


void WriteOtherCv(uint8_t cv_num, uint32_t new_adc_reading) {
  static volatile uint16_t voltage_smoothers[8];

  uint16_t adc_reading = (uint16_t) (new_adc_reading & 0xfff) << 4;
  add_data[cv_num] = apply_voltage_smoother(adc_reading, &voltage_smoothers[cv_num]);
}

void WriteOtherCvWithoutSmoothing(uint8_t cv_num, uint32_t new_adc_reading) {
  add_data[cv_num] = new_adc_reading;
}

void PrecomputeCalibration(void) {
  for(uint8_t i = 0; i < 8; i++) {
    if (cal_constants[i] < 100) {
      // Load time: fix anything weirdly low (or disconnected, or read back
      // from a blank/erased eprom) so we never divide by a tiny number.
      // The stricter <500 capture-time guard lives in ControllerCalibrationLoop.
      cal_constants[i] = 4095;
    }
    external_cal[i] = 4095.0 / (float) cal_constants[i];
  }
}

