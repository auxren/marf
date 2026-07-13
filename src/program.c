#include "program.h"

#include <stm32f4xx.h>

#include "analog_data.h"
#include "HC165.h"
#include "afg.h"
#include "scales.h"
#include "turing.h"   // marf_rand()

// Main step programming
volatile uStep steps[32];
volatile StepSliders sliders[32];
volatile PinnedSliders voltage_slider_pins;
volatile PinnedSliders time_slider_pins;

// Live (raw) slider positions, updated even while the committed sliders[] values
// are frozen. Used by the scale-select gesture to read the physical slider
// without disturbing the stage's voltage/time.
volatile uint16_t slider_raw_v[32];
volatile uint16_t slider_raw_t[32];

// When set, slider readings are tracked into slider_raw_* but NOT committed to
// sliders[] (so holding Quantize to pick a scale doesn't move any stage).
volatile uint8_t scale_select_freeze = 0;

// Per-slider two-point calibration: calibrated = (raw - off) * scale, clamped.
// Defaults (off 0, scale 1) are passthrough = raw, i.e. today's behaviour.
#define SLIDER_CAL_MIN_SPAN 1000
static float slider_v_off[32], slider_v_scale[32];
static float slider_t_off[32], slider_t_scale[32];

static inline uint16_t slider_cal_apply(float off, float scale, uint16_t raw) {
  if (scale <= 0.0f) return raw;                 // safety / passthrough
  float v = ((float) raw - off) * scale;
  if (v < 0.0f) v = 0.0f;
  if (v > 4095.0f) v = 4095.0f;
  return (uint16_t) (v + 0.5f);
}

// Reset slider calibration to passthrough (raw 0..4095).
void ClearSliderCalibration(void) {
  for (uint8_t i = 0; i < 32; i++) {
    slider_v_off[i] = 0.0f; slider_v_scale[i] = 1.0f;
    slider_t_off[i] = 0.0f; slider_t_scale[i] = 1.0f;
  }
}

// Apply two-point slider calibration from captured min/max. A slider whose span
// is implausibly small falls back to passthrough so a botched pass can't kill it.
void SetSliderCalibration(const uint16_t *v_min, const uint16_t *v_max,
                          const uint16_t *t_min, const uint16_t *t_max) {
  for (uint8_t i = 0; i < 32; i++) {
    int vs = (int) v_max[i] - (int) v_min[i];
    if (vs >= SLIDER_CAL_MIN_SPAN) {
      slider_v_off[i] = (float) v_min[i]; slider_v_scale[i] = 4095.0f / (float) vs;
    } else { slider_v_off[i] = 0.0f; slider_v_scale[i] = 1.0f; }
    int ts = (int) t_max[i] - (int) t_min[i];
    if (ts >= SLIDER_CAL_MIN_SPAN) {
      slider_t_off[i] = (float) t_min[i]; slider_t_scale[i] = 4095.0f / (float) ts;
    } else { slider_t_off[i] = 0.0f; slider_t_scale[i] = 1.0f; }
  }
}

void InitProgram() {
  uStep clear_step = {{ 0x00, 0x00, 0x00 }};
  clear_step.b.TimeRange_3 = 1;   // default to the 3 s range (was 30 s)
  clear_step.b.FullRange = 1;
  clear_step.b.TuringLength = 6;  // default 8-bit register (full value range)
  clear_step.b.PulseWidth = 8;    // default ~50% gate (0 = the classic ~1 ms
                                  // trigger, which is inaudible as a gate and
                                  // made fresh/cleared programs feel dead)

  StepSliders clear_slider;
  clear_slider.VLevel = 0;
  clear_slider.TLevel = 0;

  for(uint8_t s = 0; s < 32; s++) {
    steps[s] = clear_step;
    sliders[s] = clear_slider;
  };
  unpin_all_sliders();
  ClearSliderCalibration();   // passthrough until a two-point cal is loaded
}

// Randomize one 16-step block (`section` 0 -> stages 1-16, 1 -> stages 17-32):
// every stage in that block gets random slider values, a random voltage
// range/octave, random quantize/slope/pulse states and a random time range, and
// the block gets a random loop length (First on its first stage, Last on a
// random stage within the block). Voltage and time sources are kept Internal (so
// nothing needs to be patched) and Stop/Sustain/Enable are left off so it just
// plays. Only this block's sliders are pinned (like a program load) so the
// physical positions don't immediately override them -- the other block (the
// other AFG) is left completely untouched. The PRNG should be reseeded by the
// caller.
void RandomizeProgram(uint8_t section) {
  uint8_t base = (section & 1) << 4;             // 0 or 16
  uint8_t last_off = 1 + (marf_rand() % 15);     // 1..15: loop length within block

  for (uint8_t off = 0; off < 16; off++) {
    uint8_t i = base + off;
    uStep s = {{ 0, 0, 0, 0 }};
    uint32_t r = marf_rand();

    // Voltage range / octave: exactly one of Full / 0 / 2 / 4 / 6 / 8.
    switch (r % 6) {
      case 0:  s.b.FullRange = 1; break;
      case 1:  s.b.Voltage0  = 1; break;
      case 2:  s.b.Voltage2  = 1; break;
      case 3:  s.b.Voltage4  = 1; break;
      case 4:  s.b.Voltage6  = 1; break;
      default: s.b.Voltage8  = 1; break;
    }
    s.b.Quantize     = (r >> 3) & 1;
    s.b.Sloped       = (r >> 4) & 1;
    s.b.OutputPulse1 = (r >> 5) & 1;
    s.b.OutputPulse2 = (r >> 6) & 1;
    s.b.PulseWidth   = (r >> 12) & 0xF;   // random per-step pulse/gate length

    // Time range: one of the three faster ranges (never the slow 30 s range).
    switch ((r >> 8) % 3) {
      case 0:  s.b.TimeRange_p03 = 1; break;
      case 1:  s.b.TimeRange_p3  = 1; break;
      default: s.b.TimeRange_3   = 1; break;
    }

    // Keep sources internal and op-modes off; mark the random loop length.
    s.b.VoltageSource = 0;
    s.b.TimeSource    = 0;
    s.b.TuringLength  = 6;   // default 8-bit register (full value range)
    s.b.CycleFirst    = (off == 0)        ? 1 : 0;
    s.b.CycleLast     = (off == last_off) ? 1 : 0;

    steps[i] = s;
    sliders[i].VLevel = marf_rand() & 0x0FFF;
    sliders[i].TLevel = marf_rand() & 0x0FFF;

    // Pin just this block's sliders to their new values.
    voltage_slider_pins.high |= (1UL << i);
    voltage_slider_pins.low  |= (1UL << i);
    time_slider_pins.high    |= (1UL << i);
    time_slider_pins.low     |= (1UL << i);
  }
}

float GetStepVoltage(uint8_t section, uint8_t step_num, uint8_t scale, uint8_t root,
                     uint16_t override_value, uint8_t use_override) {
  float voltage_level = 0.0; // stay in floating point throughout!
  uint8_t ext_ban_num = 0;
  uint8_t slider_num = step_num;

  step_num += section << 4; // section select

  if (use_override) {
    // Voltage supplied by the caller (e.g. a Turing machine). Octave/range/
    // quantize below still apply on top.
    voltage_level = (float) override_value;
  } else if (steps[step_num].b.VoltageSource) {
    // External source, soft-normalled: use the external input when a CV is
    // present, otherwise fall back to this step's slider value.
    ext_ban_num = sliders[slider_num].VLevel >> 10;
    if (external_present[ext_ban_num]) {
      voltage_level = read_calibrated_add_data_float(ext_ban_num);
    } else {
      voltage_level = (float) sliders[slider_num].VLevel;
    }
  } else {
    // Step voltage is set by slider
    voltage_level = (float) sliders[slider_num].VLevel;
  };

  // Clamp if smoothing or something has gone awry
  if (voltage_level > 4095.0) {
    voltage_level = 4095.0;
  } else if (voltage_level < 0.0) {
    voltage_level = 0.0;
  }

  if (!steps[step_num].b.FullRange) {
    // Scale voltage for limited range
    voltage_level *= limited_range_multiplier;
    if (steps[step_num].b.Voltage2) {
      voltage_level += octave_offset;
    } else if (steps[step_num].b.Voltage4) {
      voltage_level += octave_offset * 2;
    } else if (steps[step_num].b.Voltage6) {
      voltage_level += octave_offset * 3;
    } else if (steps[step_num].b.Voltage8) {
      voltage_level += octave_offset * 4;
    }
  }

  if (steps[step_num].b.Quantize) {
    // Quantize to the nearest note of the active scale.
    // Convert to a chromatic semitone index (rounded to nearest), snap that
    // into the selected scale, then convert back to a voltage. The default
    // SCALE_CHROMATIC snaps to the nearest semitone -- the original behaviour.
    int semitone = (int) (voltage_level * quantizer_magic + 0.5f);
    semitone = scale_quantize_semitone(scale, root, semitone);
    voltage_level = (float) semitone * semitone_offset;
  }

  return voltage_level;
};

uint32_t GetStepWidth(uint8_t section, uint8_t step_num, float time_multiplier) {
  float step_width = 0.0;
  float time_level = 0.0;
  volatile uint8_t ext_ban_num = 0;
  uint8_t slider_num = step_num;

  step_num += section << 4; // section select

  if (steps[step_num].b.TimeSource) {
    // Step time is set externally
    ext_ban_num = sliders[slider_num].TLevel >> 10;
    time_level = read_calibrated_add_data_float(ext_ban_num);
  } else {
    // Step time is set on panel
    time_level = (float) sliders[slider_num].TLevel;
  };

  // NOTE: the original v1.6 firmware applied a log2 taper to the time fader here
  // (gated _TIME_FADER_TIME__LOG2). We intentionally use the LINEAR mapping for
  // both v1 and v2 so the two boards run at the same rate and the time ranges
  // overlap smoothly. (The classic v1 taper lives on in the 2.x branch.)

  // Map the slider (0..4095) onto the 2-30s range for this time range:
  // 2s at minimum, rising to 2 + 28 = 30s at full scale.
  step_width = (time_level * RECIPROCAL_12BIT * STEP_WIDTH_28S) + STEP_WIDTH_2S;

  if (steps[step_num].b.TimeRange_p03 == 1) {
    step_width *= 0.001;
  } else if (steps[step_num].b.TimeRange_p3 == 1) {
    step_width *= 0.01;
  } else if (steps[step_num].b.TimeRange_3 == 1) {
    step_width *= 0.1;
  }

  return (uint32_t) (step_width * time_multiplier + 0.5);
};

// True if slider is pinned after reloading a program
inline static uint8_t is_slider_pinned(const volatile PinnedSliders* slider_pins, uint8_t slider_num) {
  uint32_t check_bit = 1 << slider_num;
  return (slider_pins->high & check_bit) || (slider_pins->low & check_bit);
}

// Write new voltage slider value, un-pinning slider if needed
void WriteVoltageSlider(uint8_t slider_num, uint32_t new_adc_reading) {
  static volatile uint16_t voltage_smoothers[32];

  uint16_t adc_reading = (uint16_t) (new_adc_reading & 0xfff);
  uint16_t smoothed = apply_voltage_smoother(adc_reading << 4, &voltage_smoothers[slider_num]);
  smoothed = slider_cal_apply(slider_v_off[slider_num], slider_v_scale[slider_num], smoothed);

  slider_raw_v[slider_num] = smoothed;     // always track the live position
  if (scale_select_freeze) return;          // frozen: don't commit or unpin

  if (smoothed >= sliders[slider_num].VLevel) {
    voltage_slider_pins.high &= ~(1UL << slider_num);
  }
  if (smoothed <= sliders[slider_num].VLevel) {
    voltage_slider_pins.low &= ~(1UL << slider_num);
  }
  if (!is_slider_pinned(&voltage_slider_pins, slider_num)) {
    sliders[slider_num].VLevel = smoothed;
  }
}

// Write new time slider value, un-pinning slider if needed
void WriteTimeSlider(uint8_t slider_num, uint32_t new_adc_reading) {
  static volatile uint16_t time_smoothers[32];

  uint16_t adc_reading = (uint16_t) (new_adc_reading & 0xfff) << 4;
  uint16_t smoothed = apply_voltage_smoother(adc_reading, &time_smoothers[slider_num]);
  smoothed = slider_cal_apply(slider_t_off[slider_num], slider_t_scale[slider_num], smoothed);

  slider_raw_t[slider_num] = smoothed;     // always track the live position
  if (scale_select_freeze) return;          // frozen: don't commit or unpin

  if (smoothed >> 4 >= sliders[slider_num].TLevel >> 4) {
    time_slider_pins.high &= ~(1UL << slider_num);
  }
  if (smoothed >> 4 <= sliders[slider_num].TLevel >> 4) {
    time_slider_pins.low &= ~(1UL << slider_num);
  }
  if (!is_slider_pinned(&time_slider_pins, slider_num)) {
    sliders[slider_num].TLevel = smoothed;
  }
}

// Calculate the number of next step.
// In the event that the end of a loop is reached,
// the closest previous "first" step is next (or 0 if none).
uint8_t GetNextStep(uint8_t section, uint8_t step_num) {
  uint8_t step_zero = section << 4;
  uint8_t next_step = step_num + step_zero;
  uint8_t max_step = get_max_step() + step_zero;
  step_num += step_zero;

  if (steps[step_num].b.CycleLast) {
    // Current step is the end of a loop.
    // Search backwards to the closest previous first step or 0
    while (next_step > 0) {
      if (steps[next_step].b.CycleFirst) break;
      next_step--;
    };
  } else {
    // Otherwise just advance 1 and check for wrap around
    next_step++;
    if (next_step > max_step) {
      next_step = step_zero;
    }
  }
  next_step -= step_zero;
  return next_step;
};

// Apply switch programming directly to the step data
// Low value means that the switch is selected/active
void ApplyProgrammingSwitches(uint8_t section, uint8_t step_num, uButtons* switches) {
  volatile uStep* step;

  step_num += section << 4; // section select
  step = &steps[step_num];

  if ( !switches->b.Voltage0 ) {
    step->b.Voltage0 = 1;
    step->b.Voltage2 = 0;
    step->b.Voltage4 = 0;
    step->b.Voltage6 = 0;
    step->b.Voltage8 = 0;
    step->b.FullRange = 0;
  };
  if ( !switches->b.Voltage2 ) {
    step->b.Voltage0 = 0;
    step->b.Voltage2 = 1;
    step->b.Voltage4 = 0;
    step->b.Voltage6 = 0;
    step->b.Voltage8 = 0;
    step->b.FullRange = 0;
  };
  if ( !switches->b.Voltage4 ) {
    step->b.Voltage0 = 0;
    step->b.Voltage2 = 0;
    step->b.Voltage4 = 1;
    step->b.Voltage6 = 0;
    step->b.Voltage8 = 0;
    step->b.FullRange = 0;
  };
  if ( !switches->b.Voltage6 ) {
    step->b.Voltage0 = 0;
    step->b.Voltage2 = 0;
    step->b.Voltage4 = 0;
    step->b.Voltage6 = 1;
    step->b.Voltage8 = 0;
    step->b.FullRange = 0;
  };
  if ( !switches->b.Voltage8 ) {
    step->b.Voltage0 = 0;
    step->b.Voltage2 = 0;
    step->b.Voltage4 = 0;
    step->b.Voltage6 = 0;
    step->b.Voltage8 = 1;
    step->b.FullRange = 0;
  };
  if ( !switches->b.FullRangeOn ) {
    step->b.Voltage0 = 0;
    step->b.Voltage2 = 0;
    step->b.Voltage4 = 0;
    step->b.Voltage6 = 0;
    step->b.Voltage8 = 0;
    step->b.FullRange = 1;
  };
  if ( !switches->b.Pulse1On ) {
    step->b.OutputPulse1 = 1;
  };
  if ( !switches->b.Pulse1Off ) {
    step->b.OutputPulse1 = 0;
  };
  if ( !switches->b.Pulse2On ) {
    step->b.OutputPulse2 = 1;
  };
  if ( !switches->b.Pulse2Off ) {
    step->b.OutputPulse2 = 0;
  };
  if ( !switches->b.OutputQuantize ) {
    step->b.Quantize = 1;
  };
  if ( !switches->b.OutputContinuous ) {
    step->b.Quantize = 0;
  };
  if ( !switches->b.IntegrationSloped ) {
    step->b.Sloped = 1;
  };
  if ( !switches->b.IntegrationStepped ) {
    step->b.Sloped = 0;
  };
  if ( !switches->b.SourceExternal ) {
    step->b.VoltageSource = 1;
  };
  if ( !switches->b.SourceInternal ) {
    step->b.VoltageSource = 0;
  };
  if ( !switches->b.StopOn ) {
    step->b.OpModeSTOP = 1;
    step->b.OpModeENABLE = 0;
    step->b.OpModeSUSTAIN = 0;
  };
  if ( !switches->b.StopOff ) {
    step->b.OpModeSTOP = 0;
  };
  if ( !switches->b.SustainOn ) {
    step->b.OpModeSUSTAIN = 1;
    step->b.OpModeSTOP = 0;
    step->b.OpModeENABLE = 0;
  };
  if ( !switches->b.SustainOff ) {
    step->b.OpModeSUSTAIN = 0;
  };
  if ( !switches->b.EnableOn ) {
    step->b.OpModeENABLE = 1;
    step->b.OpModeSTOP = 0;
    step->b.OpModeSUSTAIN = 0;
  };
  if ( !switches->b.EnableOff ) {
    step->b.OpModeENABLE = 0;
  };
  if ( !switches->b.FirstOn ) {
    step->b.CycleFirst = 1;
    step->b.CycleLast = 0;
  };
  if ( !switches->b.FirstOff ) {
    step->b.CycleFirst = 0;
  };
  if ( !switches->b.LastOn ) {
    step->b.CycleLast = 1;
    step->b.CycleFirst = 0;
  };
  if ( !switches->b.LastOff ) {
    step->b.CycleLast = 0;
  };
  if ( !switches->b.TimeSourceExternal ) {
    step->b.TimeSource = 1;
  };
  if ( !switches->b.TimeSourceInternal ) {
    step->b.TimeSource = 0;
  };
  if (!switches->b.TimeRange1) {
    step->b.TimeRange_p03 = 1;
    step->b.TimeRange_p3 =  0;
    step->b.TimeRange_3 =   0;
    step->b.TimeRange_30 =  0;
  };
  if (!switches->b.TimeRange2) {
    step->b.TimeRange_p03 = 0;
    step->b.TimeRange_p3 =  1;
    step->b.TimeRange_3 =   0;
    step->b.TimeRange_30 =  0;
  };
  if (!switches->b.TimeRange3) {
    step->b.TimeRange_p03 = 0;
    step->b.TimeRange_p3 =  0;
    step->b.TimeRange_3 =   1;
    step->b.TimeRange_30 =  0;
  };
  if (!switches->b.TimeRange4) {
    step->b.TimeRange_p03 = 0;
    step->b.TimeRange_p3 =  0;
    step->b.TimeRange_3 =   0;
    step->b.TimeRange_30 =  1;
  };
}

