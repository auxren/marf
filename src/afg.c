#include "afg.h"

#include <stm32f4xx.h>

#include "analog_data.h"
#include "display.h"
#include "MAX5135.h"
#include "program.h"
#include "scales.h"
#include "turing.h"
#include "cycle_counter.h"
#include "delays.h"

// Afg state. Volatile since control is frequently exchanged between interrupts and main loop.
volatile AfgState afg1;
volatile AfgState afg2;

// Convenience for addressing the two afgs from a uint8_t that is 0 or 1
volatile AfgState *afgs[2] = { &afg1, &afg2 };

void AfgAllInitialize() {
  afg1.section = 0;
  afg1.step_num = 0;
  afg1.step_width = 0xFFFFFF00;
  afg1.step_cnt = 0xFFFFFFFF;
  afg1.mode = MODE_STOP;
  afg1.prev_mode = MODE_STOP;
  afg1.stage_address = 0;
  afg1.step_level = 0;
  afg1.prev_step_level = 0;

  afg2.section = 0;
  afg2.step_num = 0;
  afg2.step_width = 0xFFFFFF00;
  afg2.step_cnt = 0xFFFFFFFF;
  afg2.mode = MODE_STOP;
  afg2.prev_mode = MODE_STOP;
  afg2.stage_address = 0;
  afg2.step_level = 0;
  afg2.prev_step_level = 0;
}

AfgControllerState AfgGetControllerState(uint8_t afg_num) {
  AfgState *afg = (AfgState *) afgs[afg_num];
  AfgControllerState state;
  state.mode = afg->mode;
  state.section = afg->section;
  state.step_num = afg->step_num;
  return state;
}

void AfgSetSection(uint8_t afg_num, uint8_t section) {
  AfgState *afg = (AfgState *) afgs[afg_num];
  afg->section = section;
}

// Start Timer 3 for AFG1 start pulse duration measurement
// Only used when going into enable/sustain
void InitStart_1_SignalTimer() {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

  TIM3->PSC = AFG_TIMER_PRESCALER;
  TIM3->ARR = 1;
  TIM3->CNT = 0;
  TIM3->DIER = TIM_DIER_UIE;
  TIM3->CR1 |= TIM_CR1_CEN;

  NVIC_EnableIRQ(TIM3_IRQn);
};

// Start Timer 7 for AFG2 start pulse duration measurement
// Only used when going into enable/sustain
void InitStart_2_SignalTimer() {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

  TIM7->PSC = AFG_TIMER_PRESCALER;
  TIM7->ARR = 1;
  TIM7->CNT = 0;
  TIM7->DIER = TIM_DIER_UIE;
  TIM7->CR1 |= TIM_CR1_CEN;

  NVIC_EnableIRQ(TIM7_IRQn);
};

void InitStartSignalTimer(uint8_t afg_num) {
  if (afg_num == AFG1) InitStart_1_SignalTimer();
  if (afg_num == AFG2) InitStart_2_SignalTimer();
}

#define TIME_MULTIPLIER_SCALER 0.0009766

float GetTimeMultiplier(uint8_t afg_num) {
  if (afg_num == AFG1) {
    return scale_time_fake_log2(read_calibrated_add_data_float(ADC_TIMEMULTIPLY_Ch_1)) * TIME_MULTIPLIER_SCALER;
  } else if (afg_num == AFG2) {
    return scale_time_fake_log2(read_calibrated_add_data_float(ADC_TIMEMULTIPLY_Ch_2)) * TIME_MULTIPLIER_SCALER;
  } else {
    return 0;
  }
}

// Compute continuous step stage selection

static inline void ComputeContinuousStep(uint8_t afg_num) {
  if (afg_num == AFG1) {
    afg1.stage_address = read_calibrated_add_data_uint16(ADC_STAGEADDRESS_Ch_1) >> get_max_step_shift12();
    if (afg1.stage_address > get_max_step()) afg1.stage_address = get_max_step();
  } else if (afg_num == AFG2) {
    afg2.stage_address = read_calibrated_add_data_uint16(ADC_STAGEADDRESS_Ch_2) >> get_max_step_shift12();
    if (afg2.stage_address > get_max_step()) afg2.stage_address = get_max_step();
  }
}

void AfgRecalculateStepWidths() {
  afg1.step_width = GetStepWidth(afg1.section, afg1.step_num, GetTimeMultiplier(AFG1));
  afg2.step_width = GetStepWidth(afg2.section, afg2.step_num, GetTimeMultiplier(AFG2));
}

// Triggered by timer when waiting on sustain or enable step.
// Returns 1 when starting running again
uint8_t AfgCheckStart(uint8_t afg_num, uint8_t start_signal) {
  AfgState *afg = (AfgState *) afgs[afg_num];
  uint8_t run_again = 0;

  if (afg->mode == MODE_WAIT_HI_Z && start_signal) {
    // Enable with start high, start running
    run_again = 1;
  }
  if (afg->mode == MODE_STAY_HI_Z && !start_signal) {
    // Sustain step with start low, start running again
    run_again = 1;
  }
  if (run_again) {
    afg->mode = MODE_RUN;
    afg->step_num = GetNextStep(afg->section, afg->step_num);
    afg->step_width = GetStepWidth(afg->section, afg->step_num, GetTimeMultiplier(afg_num));
    afg->step_cnt = 0;
    return 1;
  } else {
    return 0;
  }
}

void AfgJumpToStep(uint8_t afg_num, uint8_t step) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  // Sample and hold current output voltage value.
  afg->prev_step_level = afg->step_level;

  // Then update the step number to where ever we are strobing to
  afg->step_num = step;
  afg->step_width = GetStepWidth(afg->section, afg->step_num, GetTimeMultiplier(afg_num));

  // Reset step counter
  afg->step_cnt = 0;

  // Break out of some modes
  if (afg->mode == MODE_WAIT_HI_Z || afg->mode == MODE_STAY_HI_Z) {
    afg->mode = afg->prev_mode;
  }

  update_display();
}

void AfgHardStop(uint8_t afg_num) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  afg->mode = MODE_STOP;
  afg->step_num = 0;
  afg->step_cnt = 0xFFFFFFFF;
}

void AfgReset(uint8_t afg_num) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  if (afg->mode != MODE_WAIT) {
    AfgJumpToStep(afg_num, 0);
  }
}

// Process start, stop and strobe pulse inputs in reaction to an interrupt on any of them.
// Since simultaneous pulses are meaningful, we process them all together.
void AfgProcessModeChanges(uint8_t afg_num, PulseInputs pulses) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  if (pulses.strobe) {
    // Strobe to a step
    ComputeContinuousStep(afg_num);
    AfgJumpToStep(afg_num, afg->stage_address);
    if (pulses.start) {
      // And start running
      afg->mode = MODE_RUN;
    }
  } else if (pulses.start && pulses.stop && afg->mode != MODE_WAIT) {
    // Stop and start together are an advance.
    // Do not change the mode, but jump immediately to the next step.
    AfgJumpToStep(afg_num, GetNextStep(afg->section, afg->step_num));
  } else if (pulses.start) {
    if (afg->mode == MODE_STOP) {
      // Start running
      afg->mode = MODE_RUN;
      afg->step_num = GetNextStep(afg->section, afg->step_num);
      afg->step_width = GetStepWidth(afg->section, afg->step_num, GetTimeMultiplier(afg_num));
      afg->step_cnt = 0;
      update_display();
    } else if (afg->mode == MODE_WAIT_HI_Z || afg->mode == MODE_STAY_HI_Z) {
      // If on sustain or enable step, check after timer
      InitStartSignalTimer(afg_num); // Calls AfgCheckStart() later
    }
  } else if (pulses.stop && afg->mode == MODE_RUN) {
    afg->mode = MODE_STOP;
    update_display();
  }
}

// Every tick triggers new output voltages and a check if the step has ended.
ProgrammedOutputs AfgTick(uint8_t afg_num, PulseInputs pulses, uint8_t ticks) {
  volatile AfgState *afg = (AfgState *) afgs[afg_num];

  uStep step = get_step_programming(afg->section, afg->step_num);
  float output_voltage = 0;
  ProgrammedOutputs outputs;

  // step_width is kept current for the active step by the main loop
  // (AfgRecalculateStepWidths). We only recompute it here when the step
  // actually changes, to keep the float math and ADC read out of the
  // 32kHz interrupt hot path for long steps.
  if (afg->step_cnt < afg->step_width) {
    afg->step_cnt += ticks;
  }

  // Check if we're at the end of the step
  if (afg->mode == MODE_WAIT) {
    // Compute continuous stage address
    ComputeContinuousStep(afg_num);

    // Check if the step has changed by the stage address, not the timer.
    if (afg->step_num != afg->stage_address) {
      // Sample and hold current voltage output value
      afg->prev_step_level = afg->step_level;
      afg->step_num = afg->stage_address;
      step = get_step_programming(afg->section, afg->step_num);
      afg->step_width = GetStepWidth(afg->section, afg->step_num, GetTimeMultiplier(afg_num));
      // Reset step counter
      afg->step_cnt = 0;
      update_display();
    }
  } else if (afg->step_cnt >= afg->step_width) {
    // Sample and hold current step value into PreviousStep for next step slope computation
    afg->prev_step_level = afg->step_level;

    // Pin step count to max value to stop the reference
    afg->step_cnt = 0xFFFFFFFF;

    if (step.b.OpModeSTOP) {
      // Stop step
      afg->mode = MODE_STOP;
      // Don't reset step counter
    };

    if (step.b.OpModeENABLE && afg->mode != MODE_WAIT_HI_Z)  {
      // Enable step, check start banana
      if (pulses.start == 0) {
        // Go into enable mode
        afg->prev_mode = afg->mode;
        afg->mode = MODE_WAIT_HI_Z;
      };
      // Reset step counter
      afg->step_cnt = 0;
    };

    if (step.b.OpModeSUSTAIN && afg->mode != MODE_STAY_HI_Z) {
      // Sustain step, check start banana
      if (pulses.start == 1) {
        // Go into sustain mode
        afg->prev_mode = afg->mode;
        afg->mode = MODE_STAY_HI_Z;
        InitStartSignalTimer(afg_num); // Check continuously by timer for start to go low
      };
      // Don't reset step counter
    };

    if (afg->mode == MODE_RUN) {
      // Advance to the next step
      afg->step_num = GetNextStep(afg->section, afg->step_num);
      step = get_step_programming(afg->section, afg->step_num);
      afg->step_width = GetStepWidth(afg->section, afg->step_num, GetTimeMultiplier(afg_num));
      afg->step_cnt = 0; // Reset
      update_display();
    };
  };

  // Now set output voltages
  // Compute the current step's programmed voltage output
  // In Turing mode, an external-source step plays this stage's shift register
  // instead of its external input. (Other external-source steps are soft-
  // normalled to their slider value inside GetStepVoltage.)
  uint8_t turing_global = afg->step_num + (afg->section << 4);
  uint8_t use_override = turing_enabled[afg_num] && step.b.VoltageSource;
  uint16_t override_v = use_override ? turing_value(&turing_machines[turing_global]) : 0;
  output_voltage = GetStepVoltage(afg->section, afg->step_num,
                                  afg_scale[afg_num], afg_root[afg_num],
                                  override_v, use_override);
  afg->step_level = output_voltage;

  // Set AFG time out value. v1 has a 12-bit Time/Ref DAC, v2 a 10-bit one.
#if MARF_HW == 1
  outputs.time = get_time_slider_level(afg->step_num);          // full 12 bits
#else
  outputs.time = get_time_slider_level(afg->step_num) >> 2;     // 10 bits
#endif

  if (afg->step_cnt < afg->step_width) {
    // Set AFG reference out value (slopes down to 0 over the course of the step)
#if MARF_HW == 1
    outputs.ref = 4095 - (uint16_t) (((uint64_t) afg->step_cnt << 12) / afg->step_width);
#else
    outputs.ref = 1023 - (uint16_t) (((uint64_t) afg->step_cnt << 10) / afg->step_width);
#endif

    // If the step is sloped, then slope from prev_step_level to the new output value
    outputs.sloped = step.b.Sloped;
    if (step.b.Sloped ) {
      // Interpolate from the previous step level to the new step level
      afg->step_level = afg->prev_step_level +
          (output_voltage - afg->prev_step_level) * ((float) afg->step_cnt / (float) afg->step_width);
    }
  } else {
    // No reference output when not running
    outputs.ref = 0;
  }

  outputs.voltage = afg->step_level;

  // Pulse / gate length for all three pulse outputs.
  //   PulseWidth 0      -> a short FIXED trigger (~1 ms), like the classic
  //                        firmware: tempo-independent, not a fraction of the step.
  //   PulseWidth 1..15  -> a gate growing to ~99% of the step.
  // The pulse must always return low before the step ends so it can re-trigger,
  // but the low gap is capped at 1/4 of the step: at long steps it's a fixed
  // ~1 ms gap; at short steps (fast / low Time Multiply) the gap shrinks with the
  // step so the pulse never gets crushed to nothing (it stays a usable fraction
  // of the step) and never fuses into a constant high.
  uint32_t sw = afg->step_width;
  uint32_t pulse_ticks;
  if (step.b.PulseWidth == 0) {
    pulse_ticks = PULSE_ACTIVE_STEP_WIDTH;            // fixed short trigger
  } else {
    uint32_t pw_percent = 1u + ((uint32_t) step.b.PulseWidth * 98u) / 15u;
    pulse_ticks = (uint32_t) (((uint64_t) sw * pw_percent) / 100u);
    if (pulse_ticks < PULSE_ACTIVE_STEP_WIDTH) pulse_ticks = PULSE_ACTIVE_STEP_WIDTH;
  }
  // Low gap: ~1 ms, but never more than a quarter of the step.
  uint32_t gap = PULSE_ACTIVE_STEP_WIDTH;
  if (gap > sw / 4u) gap = sw / 4u;
  uint32_t max_high = (sw > gap) ? (sw - gap) : 0u;
  if (pulse_ticks > max_high) pulse_ticks = max_high;
  uint8_t pulses_on = (afg->step_cnt < pulse_ticks) ? 1 : 0;
  outputs.all_pulses = pulses_on;
  outputs.pulse1 = pulses_on ? step.b.OutputPulse1 : 0;
  outputs.pulse2 = pulses_on ? step.b.OutputPulse2 : 0;

  return outputs;
};

void EnableContinuousStageAddress(uint8_t afg_num) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  if (afg->mode != MODE_WAIT) {
    afg->prev_mode = afg->mode;
    afg->mode = MODE_WAIT;
    update_main_display();
  }
}

void DisableContinuousStageAddress(uint8_t afg_num) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  if (afg->mode == MODE_WAIT) {
    afg->mode = afg->prev_mode;
    afg->prev_mode = MODE_WAIT;
    update_display();
  }
}

uint32_t AfgGetStepTicksRemaining(uint8_t afg_num) {
  AfgState *afg = (AfgState *) afgs[afg_num];

  if (afg->step_cnt < afg->step_width) {
    return afg->step_width - afg->step_cnt;
  } else {
    return 0xFFFFFFFF;
  }
}

