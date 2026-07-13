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
#include "clockfollow.h"
#include "constants.h"

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

// ---------------------------------------------------------------------------
// External clock follow.
//
// A stream of simultaneous Start+Stop (advance) pulses with a stable spacing
// locks the generator to that clock. While locked:
//   * the Time Multiplier knob picks an integer ratio (see clockfollow.h):
//     divide counts incoming pulses, multiply subdivides the measured period
//     internally and re-syncs on every pulse (so it cannot drift);
//   * step widths come from the measured period, not the sliders/knob;
//   * the time sliders become a per-step "humanize" depth: slider down = the
//     step lands right on the clock; raising it adds a random timing offset,
//     freshly drawn (and latched) each time the step plays. A positive draw
//     fires late (a "pending" countdown started by its boundary pulse), a
//     negative draw fires early (a "rush": the previous step's width is
//     shortened so the internal timer advances just before the pulse; the
//     pulse then only clears the rushed flag).
// No pulse for CF_TIMEOUT_MS drops the lock and normal free-run resumes.
// ---------------------------------------------------------------------------

#define CF_CYCLES_PER_TICK (168000000u / AFG_TICK_FREQUENCY)   // DWT cycles per AFG tick

typedef struct {
  uint8_t  active;         // locked to an external clock
  int8_t   ratio;          // -8..-2 divide, +1 unity, +2..+8 multiply
  uint8_t  pulse_count;    // pulses since the last divide boundary
  uint8_t  subs_done;      // sub-steps emitted since the last boundary (multiply)
  uint8_t  rushed;         // boundary step was advanced early (offset < 0)
  int32_t  pending;        // ticks until a delayed (late) advance; 0 = idle
  int32_t  cur_offset;     // humanize offset the CURRENT step started with
  int32_t  next_unit;      // latched per-occurrence random unit (-4095..+4095)
  int32_t  next_offset;    // next_unit scaled by the LIVE slider depth
  uint32_t period;         // measured clock period in AFG ticks
  uint32_t last_pulse_ms;  // for the drop-out timeout
  uint32_t last_stamp;     // DWT cycle stamp of the previous pulse (0 = none)
} ClockFollowState;

static volatile ClockFollowState cfs[2];

// Live time-slider position for the humanize depth. NOT the committed/pinnable
// value: after a program load, randomize or a slider gesture the committed
// value is pinned and stops tracking the physical slider, which left some
// sliders' shuffle dead until they crossed their stored value. While locked,
// the slider IS the humanize amount - position is truth.
static inline uint16_t cf_nudge_slider(uint8_t step_num) {
  return slider_raw_t[step_num];
}

// Recompute the upcoming step's offset: the latched per-occurrence unit
// scaled by the LIVE slider depth of whatever step is next. Runs continuously
// (from the main loop) so raising/lowering a slider is heard immediately, not
// one pass later; only the random unit is fixed per occurrence, so the
// rush/late direction can't flip mid-step (the unit's sign is stable).
static void CfRefreshNextOffset(uint8_t afg_num) {
  volatile AfgState *afg = afgs[afg_num];
  volatile ClockFollowState *cf = &cfs[afg_num];
  uint8_t next = GetNextStep(afg->section, afg->step_num);
  cf->next_offset = cf_humanize_offset(
      cf->next_unit,
      cf_humanize_depth(cf_nudge_slider(next),
                        cf_base_width(cf->period, cf->ratio)));
}

// Rotate the humanize state at each clocked advance: the offset the new step
// actually started with becomes cur_offset, and a fresh random unit is drawn
// for the step after it.
static void CfDrawNextOffset(uint8_t afg_num) {
  volatile ClockFollowState *cf = &cfs[afg_num];
  cf->cur_offset = cf->next_offset;
  cf->next_unit = cf_humanize_unit(marf_rand());
  CfRefreshNextOffset(afg_num);
}

// Effective width of a step: the clock-derived width while locked, otherwise
// the normal slider/knob width. Used everywhere afg step_width is set.
static uint32_t AfgEffStepWidth(uint8_t afg_num, uint8_t section, uint8_t step_num) {
  volatile ClockFollowState *cf = &cfs[afg_num];
  if (cf->active) {
    (void) section; (void) step_num;
    return cf_step_width(cf->period, cf->ratio, cf->cur_offset, cf->next_offset);
  }
  return GetStepWidth(section, step_num, GetTimeMultiplier(afg_num));
}

// Track the ratio knob (with hysteresis) and drop the lock when the clock
// disappears. Runs continuously from the main loop.
static void ClockFollowMaintain(uint8_t afg_num) {
  volatile ClockFollowState *cf = &cfs[afg_num];
  uint16_t knob = (uint16_t) read_calibrated_add_data_float(
      afg_num == AFG1 ? ADC_TIMEMULTIPLY_Ch_1 : ADC_TIMEMULTIPLY_Ch_2);
  cf->ratio = cf_ratio_from_knob(knob, cf->ratio);
  if (cf->active) CfRefreshNextOffset(afg_num);   // live slider -> live depth
  if (cf->active && (get_millis() - cf->last_pulse_ms) > CF_TIMEOUT_MS) {
    cf->active = 0;
    cf->rushed = 0;
    cf->pending = 0;
    cf->pulse_count = 0;
    cf->subs_done = 0;
    cf->cur_offset = 0;
    cf->next_unit = 0;
    cf->next_offset = 0;
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
  ClockFollowMaintain(AFG1);
  ClockFollowMaintain(AFG2);
  afg1.step_width = AfgEffStepWidth(AFG1, afg1.section, afg1.step_num);
  afg2.step_width = AfgEffStepWidth(AFG2, afg2.section, afg2.step_num);
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
    afg->step_width = AfgEffStepWidth(afg_num, afg->section, afg->step_num);
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
  afg->step_width = AfgEffStepWidth(afg_num, afg->section, afg->step_num);

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

// Handle one external advance (Start+Stop) pulse: measure the clock, manage
// the lock, and advance according to the current ratio and nudge. `stamp` is
// the DWT cycle count captured when the pulse interrupt fired; 0 = manual
// (panel Advance), which never affects the clock lock.
static void AfgClockAdvance(uint8_t afg_num, uint32_t stamp) {
  AfgState *afg = (AfgState *) afgs[afg_num];
  volatile ClockFollowState *cf = &cfs[afg_num];

  if (stamp == 0) {
    // Panel advance: behave exactly as always.
    AfgJumpToStep(afg_num, GetNextStep(afg->section, afg->step_num));
    return;
  }

  // Period measurement / lock detection.
  uint32_t now_ms = get_millis();
  uint32_t delta_ticks = (stamp - cf->last_stamp) / CF_CYCLES_PER_TICK;
  uint8_t qualified = cf->last_stamp != 0 &&
                      (now_ms - cf->last_pulse_ms) <= CF_TIMEOUT_MS &&
                      delta_ticks >= CF_MIN_PERIOD_TICKS &&
                      delta_ticks <= CF_MAX_PERIOD_TICKS;
  uint8_t fresh_lock = 0;
  cf->last_stamp = stamp;
  cf->last_pulse_ms = now_ms;
  if (qualified) {
    cf->period = cf_update_period(cf->active ? cf->period : 0, delta_ticks);
    if (!cf->active) {
      cf->active = 1;
      fresh_lock = 1;
      cf->pulse_count = 0;
      cf->subs_done = 0;
      cf->rushed = 0;
      cf->pending = 0;
      cf->cur_offset = 0;
      cf->next_unit = 0;
      cf->next_offset = 0;
    }
  }

  if (!cf->active) {
    // Not locked (first pulse, or clock out of range): classic advance.
    AfgJumpToStep(afg_num, GetNextStep(afg->section, afg->step_num));
    return;
  }

  // Divide: only every Nth pulse is a step boundary. The pulse that
  // establishes the lock advances immediately so the response feels instant.
  if (cf->ratio < 0 && !fresh_lock) {
    cf->pulse_count++;
    if (cf->pulse_count < (uint8_t) (-cf->ratio)) return;
  }
  cf->pulse_count = 0;

  // This pulse is a step boundary.
  if (cf->rushed) {
    // The boundary step already started early (nudged down): the pulse only
    // re-arms for the next boundary.
    cf->rushed = 0;
    return;
  }
  cf->pending = 0;

  // The upcoming step's latched humanize offset decides how it lands relative
  // to this pulse. (Negative offsets are handled by the rush path in AfgTick;
  // reaching here with one means the rush hadn't fired yet - advance now.)
  int32_t n = cf->next_offset;
  if (n > 0) {
    // Late onset: AfgTick counts this down, then advances.
    cf->subs_done = 0;
    cf->pending = n;
  } else {
    cf->subs_done = 1;    // the pulse-aligned (sub-)step
    AfgJumpToStep(afg_num, GetNextStep(afg->section, afg->step_num));
    CfDrawNextOffset(afg_num);
  }
}

// Process start, stop and strobe pulse inputs in reaction to an interrupt on any of them.
// Since simultaneous pulses are meaningful, we process them all together.
// `stamp` is the DWT cycle stamp captured when the pulse interrupt fired
// (non-zero), or 0 for panel/manual events.
void AfgProcessModeChanges(uint8_t afg_num, PulseInputs pulses, uint32_t stamp) {
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
    // Stop and start together are an advance. A steady stream of advance
    // pulses is an external clock: lock to it (ratio from the Time Multiplier
    // knob, humanize from the time sliders).
    AfgClockAdvance(afg_num, stamp);
  } else if (pulses.start) {
    if (afg->mode == MODE_STOP) {
      // Start running
      afg->mode = MODE_RUN;
      afg->step_num = GetNextStep(afg->section, afg->step_num);
      afg->step_width = AfgEffStepWidth(afg_num, afg->section, afg->step_num);
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

// Pulse re-fire on shift-register clocks: when the PLAYING stage's register
// is stepped (Turing mode), the stage's pulses fire again - a new value
// should be announced with a gate even though no stage transition happened
// (e.g. the sequencer parked on one stage with a clock into its register).
// The gate width scales with the REGISTER CLOCK period (not the stage timer:
// a parked stage's step width can be seconds, which held the gate high across
// clocks - "always on"). Counts ticks since the request; 0xFFFFFFFF = idle.
static volatile uint32_t pulse_refire[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
static volatile uint32_t pulse_refire_width[2] = { 0, 0 };

void AfgTuringPulseRefire(uint8_t afg_num, uint32_t period_ticks) {
  volatile AfgState *afg = afgs[afg_num];
  uStep step = get_step_programming(afg->section, afg->step_num);
  uint32_t w;
  if (step.b.PulseWidth == 0 ||
      period_ticks < 4u * PULSE_ACTIVE_STEP_WIDTH || period_ticks > 200000u) {
    // Trigger width, also the fallback when the clock period is implausible
    // (first edge, or out of musical range).
    w = PULSE_ACTIVE_STEP_WIDTH;
  } else {
    uint32_t pw_percent = 1u + ((uint32_t) step.b.PulseWidth * 98u) / 15u;
    w = (uint32_t) (((uint64_t) period_ticks * pw_percent) / 100u);
    // Always return low before the next clock so the gate can re-trigger.
    uint32_t gap = PULSE_ACTIVE_STEP_WIDTH;
    if (gap > period_ticks / 4u) gap = period_ticks / 4u;
    if (w > period_ticks - gap) w = period_ticks - gap;
    if (w < PULSE_ACTIVE_STEP_WIDTH) w = PULSE_ACTIVE_STEP_WIDTH;
  }
  pulse_refire_width[afg_num] = w;
  pulse_refire[afg_num] = 0;
}

// Advance to the next step from inside AfgTick (mirrors the classic advance).
static void AfgAdvanceInTick(volatile AfgState *afg, uint8_t afg_num, uStep *step) {
  afg->step_num = GetNextStep(afg->section, afg->step_num);
  *step = get_step_programming(afg->section, afg->step_num);
  afg->step_width = AfgEffStepWidth(afg_num, afg->section, afg->step_num);
  afg->step_cnt = 0; // Reset
  update_display();
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

  // Externally clocked: a humanized-late step boundary is armed by its clock
  // pulse and fires here once the delay has elapsed.
  volatile ClockFollowState *cf = &cfs[afg_num];
  if (cf->active && cf->pending > 0 &&
      afg->mode != MODE_WAIT && afg->mode != MODE_WAIT_HI_Z && afg->mode != MODE_STAY_HI_Z) {
    cf->pending -= ticks;
    if (cf->pending <= 0) {
      cf->pending = 0;
      cf->subs_done = 1;   // the (delayed) pulse-aligned step
      AfgJumpToStep(afg_num, GetNextStep(afg->section, afg->step_num));
      CfDrawNextOffset(afg_num);
      step = get_step_programming(afg->section, afg->step_num);
    }
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
      afg->step_width = AfgEffStepWidth(afg_num, afg->section, afg->step_num);
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

    if (!cf->active) {
      if (afg->mode == MODE_RUN) {
        // Advance to the next step
        AfgAdvanceInTick(afg, afg_num, &step);
      }
    } else if (afg->mode == MODE_RUN || afg->mode == MODE_STOP) {
      // Externally clocked: while locked, the clock is the transport (advance
      // pulses have always worked in MODE_STOP too). The internal expiry may
      // only emit multiply sub-steps or rush a humanized-early boundary;
      // otherwise it holds for the next clock pulse. Enable/Sustain holds
      // (WAIT/STAY_HI_Z) still block everything, as when free-running.
      if (cf->ratio > 1 && cf->subs_done >= 1 && cf->subs_done < (uint8_t) cf->ratio) {
        // Multiply: internal sub-step between clock pulses
        AfgAdvanceInTick(afg, afg_num, &step);
        CfDrawNextOffset(afg_num);
        cf->subs_done++;
      } else if (!cf->rushed && cf->pending == 0 && cf->next_offset < 0) {
        // Rush: the next step drew an early offset; its (shortened) width just
        // expired, so it starts now, ahead of its clock pulse.
        AfgAdvanceInTick(afg, afg_num, &step);
        CfDrawNextOffset(afg_num);
        cf->rushed = 1;
        cf->subs_done = 1;
      }
      // else: hold until the next clock pulse
    }
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
  // Gate percentages are taken from the NOMINAL step width: while locked to an
  // external clock the actual width stretches/shrinks to deliver the NEXT
  // step's humanize offset, and scaling the gate with it made a raised slider
  // audibly modulate the PREVIOUS stage's gate length. With the nominal width
  // only the silent tail absorbs the shift; the gate itself stays steady.
  uint32_t nominal = sw;
  if (cf->active) nominal = cf_base_width(cf->period, cf->ratio);
  uint32_t pulse_ticks;
  if (step.b.PulseWidth == 0) {
    pulse_ticks = PULSE_ACTIVE_STEP_WIDTH;            // fixed short trigger
  } else {
    uint32_t pw_percent = 1u + ((uint32_t) step.b.PulseWidth * 98u) / 15u;
    pulse_ticks = (uint32_t) (((uint64_t) nominal * pw_percent) / 100u);
    if (pulse_ticks < PULSE_ACTIVE_STEP_WIDTH) pulse_ticks = PULSE_ACTIVE_STEP_WIDTH;
  }
  // Low gap: ~1 ms, but never more than a quarter of the step.
  uint32_t gap = PULSE_ACTIVE_STEP_WIDTH;
  if (gap > sw / 4u) gap = sw / 4u;
  uint32_t max_high = (sw > gap) ? (sw - gap) : 0u;
  if (pulse_ticks > max_high) pulse_ticks = max_high;
  // A shift-register clock on the playing stage re-fires its pulses, scaled
  // to the register clock period, independent of the stage timer.
  if (pulse_refire[afg_num] < 0xFFFFFFFFu - ticks) {
    pulse_refire[afg_num] += ticks;
  }
  uint8_t pulses_on = (afg->step_cnt < pulse_ticks) ||
                      (pulse_refire[afg_num] < pulse_refire_width[afg_num]) ? 1 : 0;
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

