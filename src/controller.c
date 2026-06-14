#include "controller.h"

#include <stddef.h>
#include <string.h>  // memcpy
#include <stm32f4xx.h>

#include "display.h"
#include "afg.h"
#include "leds_step.h"
#include "expander.h"
#include "adc_pots_selector.h"
#include "MAX5135.h"
#include "delays.h"
#include "eprom.h"
#include "analog_data.h"
#include "watchdog.h"
#include "scales.h"
#include "turing.h"

// Step selected for editing (0-31)
volatile uint8_t edit_mode_step_num = 0;
volatile uint8_t edit_mode_section = 0;

// Jobs
volatile ControllerJobFlags controller_job_flags;

// Set while the scale-select gesture is active, so the main loop shows the
// scale number on the step LEDs instead of the normal step display.
volatile uint8_t scale_select_active = 0;
volatile uint8_t scale_select_value = 0;   // 0..SCALE_COUNT-1

// On units whose Pulse 1/2 switch inputs are physically reversed, the panel
// programs the wrong pulse. Set during calibration and stored in the cal
// record; when set, the Pulse 1/2 switch inputs are swapped before programming.
volatile uint8_t swapped_pulse_switches = 0;

inline static void adc_pause(void) {
  NVIC_DisableIRQ(ADC_IRQn);
};

inline static void adc_resume(void) {
  NVIC_EnableIRQ(ADC_IRQn);
};

// Start Timer 6 for clear switch measurement
void InitClear_Timer() {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

  TIM6->PSC = 21000;
  TIM6->ARR = 200;
  TIM6->CNT = 0;
  TIM6->DIER = TIM_DIER_UIE;
  TIM6->CR1 |= TIM_CR1_CEN;

  NVIC_EnableIRQ(TIM6_DAC_IRQn);
  TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);
};


// Functionality that needs to run both the main and modal loops
void ControllerCommonAllLoops() {
  // Pet the watchdog. This runs in the main loop and the load/save modal
  // loops, so a hang anywhere in normal operation triggers a recovery reset.
  WatchdogRefresh();

  // Expensive to recalculate every tick, so do it here
  AfgRecalculateStepWidths();

  // Shift adc mux if time
  if (controller_job_flags.adc_mux_shift_out) {
    // Disable conversion during shift
    controller_job_flags.inhibit_adc = 1;
    if (Is_Expander_Present()) {
      // Increment the slider, including expander sliders
      controller_job_flags.adc_pot_sel = AdcMuxAdvanceExpanded(controller_job_flags.adc_pot_sel);
    } else {
      // Increments the slider
      controller_job_flags.adc_pot_sel = AdcMuxAdvance(controller_job_flags.adc_pot_sel);
    }
    controller_job_flags.adc_mux_shift_out = 0;
    // Wait for settle
    delay_us(10);
    // Reenable conversion again
    controller_job_flags.inhibit_adc = 0;
  }
}

// Main Loop. Does not return.

void ControllerMainLoop() {
  uButtons switches;

  controller_job_flags.adc_pot_sel = 0;
  controller_job_flags.adc_mux_shift_out = 0;
  controller_job_flags.inhibit_adc = 0;

  // Stable switches state, post debouncing
  volatile uint64_t stable_switches_state;

  // Previous stable switches state
  volatile uint64_t previous_switches_state;

  // Raw new switches reading
  volatile uint64_t new_switches_state;

  // Time at which the switches were last scanned
  uint32_t switch_last_read_time = 0;

  // Time at which the display was updated
  uint32_t leds_update_time = 0;

  // Down counter to debounce switches
  uint16_t switch_debounce_counter = KEY_DEBOUNCE_COUNT;

  // Initial read
  stable_switches_state = previous_switches_state = HC165_ReadSwitches();
  switches.value = stable_switches_state;

  AfgAllInitialize();
  AfgTick(AFG1, get_afg1_pulse_inputs(), 1);
  AfgTick(AFG2, get_afg2_pulse_inputs(), 1);

  // Main loop. Does not return.
  while (1) {
    ControllerCommonAllLoops();

    // Scan switches every 5ms
    if (get_millis() - switch_last_read_time > 5) {
      new_switches_state = HC165_ReadSwitches(); // SLOOOOOOOOOOOW right here ..
      switch_last_read_time = get_millis();
      if (new_switches_state == stable_switches_state) {
        // Nothing is happening
        switch_debounce_counter = KEY_DEBOUNCE_COUNT;
      } else {
        // Switch state is changing
        switch_debounce_counter -=1;
      }

      // Navigation switches have their own debouncing logic.
      // Apply it every 5ms tick.
      ControllerProcessNavigationSwitches(&switches);
    }
    if (!switch_debounce_counter) {
      // Now switches are stable
      switch_debounce_counter = KEY_DEBOUNCE_COUNT;
      previous_switches_state = stable_switches_state;
      stable_switches_state = new_switches_state;

      switches.value = stable_switches_state;

      // Apply switches that should only be applied after debounce
      ControllerProcessStageAddressSwitches(&switches);

      // Clear switch might send us into another mode
      if (!switches.b.ClearUp || !switches.b.ClearDown) {
        // Wait for long press on clear
        InitClear_Timer();
      }
    }

    // Step programming can be applied continuously to be more responsive,
    // eg when the clock is running fast. (Suppressed while the Turing chord is
    // held so entering Turing mode doesn't reprogram the current step.)
    uint8_t turing_chord = !switches.b.OutputQuantize && !switches.b.SourceExternal;
    if (!turing_chord) {
      ControllerApplyProgrammingSwitches(&switches);
    }

    // Quantize-held + slider selects the sequence's scale/root
    ControllerProcessScaleSelect(&switches);

    // Sense CV presence on the external inputs (for soft-normalling).
    SenseExternalInputs();

    // Source-External + Quantize chord toggles Turing mode; external inputs
    // clock the per-stage shift registers; Source-External + slider configures
    // each stage's clock/length while in Turing mode.
    ControllerProcessTuring(&switches);
    ControllerProcessTuringClocks();
    ControllerProcessTuringConfig(&switches);

    // Holding both Stage Address Reset buttons for >1s randomizes the program.
    ControllerProcessRandomize(&switches);

    // Update panel state
    UpdateModeSectionLeds(AfgGetControllerState(AFG1), AfgGetControllerState(AFG2), edit_mode_section, edit_mode_step_num);
    display_update_flags.b.MainDisplay = 0;
    UpdateStepSectionLeds(AfgGetControllerState(AFG1), AfgGetControllerState(AFG2), edit_mode_step_num);
    display_update_flags.b.StepsDisplay = 0;

    // While selecting a scale, show the scale number on the step LEDs instead
    // (this overrides the normal step display until the gesture is released).
    if (scale_select_active) {
      steps_leds_lit = 0xFFFFFFFF & ~(1UL << scale_select_value);
    }

    // Breathe the voltage-source LED only when Turing mode is on AND the
    // focused stage is set to External (so it actually uses its Turing machine).
    // An Internal stage shows its normal (off) LED and plays its slider voltage.
    {
      AfgControllerState a1 = AfgGetControllerState(AFG1);
      AfgControllerState a2 = AfgGetControllerState(AFG2);
      uint8_t disp_afg, f_step, f_section;
      if (display_mode == DISPLAY_MODE_VIEW_2 || display_mode == DISPLAY_MODE_EDIT_2) {
        disp_afg = AFG2;
      } else {
        disp_afg = AFG1;
      }
      if (display_mode == DISPLAY_MODE_VIEW_1) {
        f_step = a1.step_num; f_section = a1.section;
      } else if (display_mode == DISPLAY_MODE_VIEW_2) {
        f_step = a2.step_num; f_section = a2.section;
      } else {  // EDIT_1 / EDIT_2
        f_step = edit_mode_step_num; f_section = edit_mode_section;
      }
      uStep fs = get_step_programming(f_section, f_step);
      mode_led_breathe = turing_enabled[disp_afg] && fs.b.VoltageSource;
    }


    // Flush LEDs every 20ms.
    // Shifting out to the leds is kind of slow, so rate limit the update to 50 Hz.
    if (get_millis() - leds_update_time > 20) {
      FlushLedUpdates();
      leds_update_time = get_millis();
    }

    // Go into modal loops if called for

    if (controller_job_flags.modal_loop == CONTROLLER_MODAL_LOAD) {
      // Sub loop for load program
      ControllerLoadProgramLoop(); // only exits when done
      delay_ms(500);
    } else if (controller_job_flags.modal_loop == CONTROLLER_MODAL_SAVE) {
      // Save program
      ControllerSaveProgramLoop(); // only exits when done
      delay_ms(500);
    } else if (controller_job_flags.modal_loop == CONTROLLER_MODAL_SCAN) {
      // Scan the adc2 inputs before processing a pulse in
      ControllerScanAdcLoop(); // only exits when done
    }

  }; // end main loop
}

#define SCALE_SELECT_MOVE_THRESHOLD 100   // ADC counts, ignores jitter

// Scale / root selection gesture.
// Holding the Quantize switch (in any view/edit mode) turns the sliders into
// scale/root selectors for the *displayed* sequence: move any voltage slider to
// pick the scale, any time slider to pick the root. The step LEDs show the
// number of whichever was last moved. On release the sliders resume normal
// control immediately. (Source-External must NOT be held -- that chord is
// reserved for shift-register mode.)
void ControllerProcessScaleSelect(uButtons * key) {
  static uint8_t active = 0;
  static uint8_t showing_root = 0;   // step LEDs show root (vs scale) when set
  static uint16_t snap_v[32], snap_t[32];  // live slider positions at entry
  static uint8_t moved_v[32], moved_t[32]; // which sliders were used to select

  uint8_t quantize_held   = !key->b.OutputQuantize;
  uint8_t source_ext_held = !key->b.SourceExternal;
  uint8_t max = get_max_step();

  if (quantize_held && !source_ext_held) {
    uint8_t afg = (display_mode == DISPLAY_MODE_VIEW_2 ||
                   display_mode == DISPLAY_MODE_EDIT_2) ? AFG2 : AFG1;

    if (!active) {
      // Entering: freeze slider commits so moving a slider can't change any
      // stage's output, and snapshot the live positions to detect movement.
      active = 1;
      showing_root = 0;
      scale_select_freeze = 1;
      for (uint8_t i = 0; i <= max; i++) {
        snap_v[i] = slider_raw_v[i];
        snap_t[i] = slider_raw_t[i];
        moved_v[i] = 0;
        moved_t[i] = 0;
      }
    }

    // A voltage slider moved past the threshold picks the scale; a time slider
    // picks the root. The step LEDs follow whichever was last moved.
    for (uint8_t i = 0; i <= max; i++) {
      int dv = (int) slider_raw_v[i] - (int) snap_v[i];
      int dt = (int) slider_raw_t[i] - (int) snap_t[i];
      if (dv < 0) dv = -dv;
      if (dt < 0) dt = -dt;
      if (dv > SCALE_SELECT_MOVE_THRESHOLD) {
        afg_scale[afg] = scale_from_slider(slider_raw_v[i]);
        showing_root = 0;
        moved_v[i] = 1;
      }
      if (dt > SCALE_SELECT_MOVE_THRESHOLD) {
        afg_root[afg] = root_from_slider(slider_raw_t[i]);
        showing_root = 1;
        moved_t[i] = 1;
      }
    }

    scale_select_value = showing_root ? afg_root[afg] : afg_scale[afg];
    scale_select_active = 1;
  } else if (active) {
    // Releasing: unfreeze, and pin the sliders we moved so each stage stays at
    // the value it had before the gesture (the physical slider is now elsewhere,
    // and the stage holds until that slider is moved back through its value).
    scale_select_freeze = 0;
    for (uint8_t i = 0; i <= max; i++) {
      if (moved_v[i]) {
        voltage_slider_pins.high |= (1UL << i);
        voltage_slider_pins.low  |= (1UL << i);
      }
      if (moved_t[i]) {
        time_slider_pins.high |= (1UL << i);
        time_slider_pins.low  |= (1UL << i);
      }
    }
    active = 0;
    scale_select_active = 0;
  }
}

// Turing mode enable chord.
// Holding Source-External AND Quantize together for ~0.8 s toggles Turing mode
// for the displayed sequence (one toggle per hold). When on, external-source
// steps take their voltage from this stage's shift register.
void ControllerProcessTuring(uButtons * key) {
  static uint8_t state = 0;        // 0 idle, 1 timing, 2 toggled (await release)
  static uint32_t start = 0;

  uint8_t both_held = !key->b.OutputQuantize && !key->b.SourceExternal;

  if (both_held) {
    if (state == 0) { state = 1; start = get_millis(); }
    else if (state == 1 && (get_millis() - start) > 800) {
      uint8_t afg = (display_mode == DISPLAY_MODE_VIEW_2 ||
                     display_mode == DISPLAY_MODE_EDIT_2) ? AFG2 : AFG1;
      turing_enabled[afg] ^= 1;
      if (turing_enabled[afg]) {
        mode_led_breathe = 0;          // avoid PWM contention during the animation
        RunTuringEnterAnimation();
      }
      state = 2;
    }
  } else {
    state = 0;
  }
}

// Holding BOTH Stage Address Reset buttons for >1 s randomizes the whole
// program (slider values, voltage ranges, quantize/slope/pulses, time ranges
// and a random loop length) and plays a ~2 s "randomizing" light show.
void ControllerProcessRandomize(uButtons * key) {
  static uint8_t state = 0;        // 0 idle, 1 timing, 2 fired (await release)
  static uint32_t start = 0;

  uint8_t both_reset = !key->b.StageAddress1Reset && !key->b.StageAddress2Reset;

  if (both_reset) {
    if (state == 0) { state = 1; start = get_millis(); }
    else if (state == 1 && (get_millis() - start) > 1000) {
      turing_seed(DWT->CYCCNT);     // fresh entropy from the cycle counter
      RandomizeProgram();
      RunRandomizeAnimation();
      AfgReset(AFG1);               // start the new sequence cleanly from step 1
      AfgReset(AFG2);
      state = 2;
    }
  } else {
    state = 0;
  }
}

// Clock the per-stage shift registers from the four external inputs.
// A rising edge on external input k clocks every stage assigned to clock k,
// with the stage's voltage slider acting as the Turing "big knob".
void ControllerProcessTuringClocks(void) {
  static uint16_t prev[4] = { 0, 0, 0, 0 };

  if (!turing_enabled[0] && !turing_enabled[1]) return;

  for (uint8_t k = 0; k < 4; k++) {
    uint16_t v = add_data[k];
    uint8_t rising = (prev[k] < 2048) && (v >= 2048);
    prev[k] = v;
    if (rising) {
      for (uint8_t s = 0; s < TURING_NUM_STAGES; s++) {
        uStep st = get_step_programming(0, s);
        if (st.b.TuringClock == k) {
          turing_set_length(&turing_machines[s], st.b.TuringLength + 2);  // 2..16
          turing_clock(&turing_machines[s], sliders[s].VLevel);           // big knob
        }
      }
    }
  }
}

// Per-stage Turing config gesture (only in Turing mode for the displayed
// sequence). Holding Source-External (without Quantize) and moving a voltage
// slider sets which of the four external jacks (0-3) clocks that stage; moving a
// time slider sets that stage's loop length (2-16). The step LEDs show the value.
// Sliders are frozen during the gesture (so the probability / duration don't
// change) and the moved ones are pinned on release.
void ControllerProcessTuringConfig(uButtons * key) {
  static uint8_t active = 0;
  static uint16_t snap_v[32], snap_t[32];
  static uint8_t moved_v[32], moved_t[32];

  uint8_t source_ext_held = !key->b.SourceExternal;
  uint8_t quantize_held   = !key->b.OutputQuantize;
  uint8_t max = get_max_step();
  uint8_t disp_afg = (display_mode == DISPLAY_MODE_VIEW_2 ||
                      display_mode == DISPLAY_MODE_EDIT_2) ? AFG2 : AFG1;

  if (source_ext_held && !quantize_held && turing_enabled[disp_afg]) {
    if (!active) {
      active = 1;
      scale_select_freeze = 1;
      for (uint8_t i = 0; i <= max; i++) {
        snap_v[i] = slider_raw_v[i];
        snap_t[i] = slider_raw_t[i];
        moved_v[i] = 0;
        moved_t[i] = 0;
      }
    }
    for (uint8_t i = 0; i <= max; i++) {
      int dv = (int) slider_raw_v[i] - (int) snap_v[i];
      int dt = (int) slider_raw_t[i] - (int) snap_t[i];
      if (dv < 0) dv = -dv;
      if (dt < 0) dt = -dt;
      if (dv > SCALE_SELECT_MOVE_THRESHOLD) {
        uint8_t clk = (uint8_t) (((uint32_t) slider_raw_v[i] * 4) / 4096);
        if (clk > 3) clk = 3;
        steps[i].b.TuringClock = clk;
        scale_select_value = clk;            // show clock 0-3 on step LEDs
        moved_v[i] = 1;
      }
      if (dt > SCALE_SELECT_MOVE_THRESHOLD) {
        uint8_t len = (uint8_t) (((uint32_t) slider_raw_t[i] * 15) / 4096);  // 0..14
        if (len > 14) len = 14;
        steps[i].b.TuringLength = len;
        scale_select_value = (uint8_t) (len + 1);  // show length-1 (1..15) on step LEDs
        moved_t[i] = 1;
      }
    }
    scale_select_active = 1;
  } else if (active) {
    scale_select_freeze = 0;
    for (uint8_t i = 0; i <= max; i++) {
      if (moved_v[i]) {
        voltage_slider_pins.high |= (1UL << i);
        voltage_slider_pins.low  |= (1UL << i);
      }
      if (moved_t[i]) {
        time_slider_pins.high |= (1UL << i);
        time_slider_pins.low  |= (1UL << i);
      }
    }
    active = 0;
    scale_select_active = 0;
  }
}

void ControllerApplyProgrammingSwitches(uButtons * key) {
  AfgControllerState afg1 = AfgGetControllerState(AFG1);
  AfgControllerState afg2 = AfgGetControllerState(AFG2);
  uint8_t step_num = 0, section = 0;

  // Determine step num for different display modes
  if (display_mode == DISPLAY_MODE_VIEW_1) {
    step_num = afg1.step_num;
    section = afg1.section;
  } else if (display_mode == DISPLAY_MODE_VIEW_2) {
    step_num = afg2.step_num;
    section = afg2.section;
  } else if (display_mode == DISPLAY_MODE_EDIT_1 || display_mode == DISPLAY_MODE_EDIT_2) {
    step_num = edit_mode_step_num;
    section = edit_mode_section;
  };

  // On units with reversed Pulse 1/2 switch wiring (selected in calibration),
  // swap the Pulse 1/2 switch inputs so the panel programs the intended output.
  if (swapped_pulse_switches) {
    uButtons k = *key;
    k.b.Pulse1On  = key->b.Pulse2On;
    k.b.Pulse1Off = key->b.Pulse2Off;
    k.b.Pulse2On  = key->b.Pulse1On;
    k.b.Pulse2Off = key->b.Pulse1Off;
    ApplyProgrammingSwitches(section, step_num, &k);
  } else {
    ApplyProgrammingSwitches(section, step_num, key);
  }
}

void ControllerProcessStageAddressSwitches(uButtons * key) {
  PulseInputs signals1 = {}, signals2 = {};

  // Only do one of reset, strobe or advance
  if (!key->b.StageAddress1Reset) {
    AfgReset(AFG1);
    update_display();
  } else if (!key->b.StageAddress1PulseSelect) {
    signals1.strobe = 1;
    AfgProcessModeChanges(AFG1, signals1);
  } else if (!key->b.StageAddress1Advance) {
    signals1.start = 1;
    signals1.stop = 1;
    AfgProcessModeChanges(AFG1, signals1);
  }

  if (!key->b.StageAddress2Reset) {
    AfgReset(AFG2);
    update_display();
  } else if ( (!key->b.StageAddress2PulseSelect)) {
    signals2.strobe = 1;
    AfgProcessModeChanges(AFG2, signals2);
  } else if (!key->b.StageAddress2Advance) {
    signals2.start = 1;
    signals2.stop = 1;
    AfgProcessModeChanges(AFG2, signals2);
  };

  if (!key->b.StageAddress1ContiniousSelect) {
    EnableContinuousStageAddress(AFG1);
  } else {
    DisableContinuousStageAddress(AFG1);
  }

  if (!key->b.StageAddress2ContiniousSelect) {
    EnableContinuousStageAddress(AFG2);
  } else {
    DisableContinuousStageAddress(AFG2);
  }
}

void ControllerProcessNavigationSwitches(uButtons* key) {
  AfgControllerState afg1 = AfgGetControllerState(AFG1);
  AfgControllerState afg2 = AfgGetControllerState(AFG2);

  // Down counters which track the number of ticks of this method
  // before left and right switches should be processed again.
  // This enables some debouncing but also long press and hold to scroll.
  static uint16_t left_counter = SHORT_COUNTER_TICKS;
  static uint16_t right_counter = SHORT_COUNTER_TICKS;

  // Display/edit mode changes
  if (!key->b.StageAddress1Display && display_mode != DISPLAY_MODE_VIEW_1) {
    display_mode = DISPLAY_MODE_VIEW_1;
  }
  if (!key->b.StageAddress2Display && display_mode != DISPLAY_MODE_VIEW_2) {
    display_mode = DISPLAY_MODE_VIEW_2;
  }
  if (!key->b.StepLeft || !key->b.StepRight) {
    if (display_mode == DISPLAY_MODE_VIEW_1) {
      display_mode = DISPLAY_MODE_EDIT_1;
      edit_mode_section = afg1.section;
      edit_mode_step_num = 0;
      right_counter = SCROLL_WAIT_COUNTER;
      left_counter = SCROLL_WAIT_COUNTER;
      update_display();
    }
    else if (display_mode == DISPLAY_MODE_VIEW_2) {
      display_mode = DISPLAY_MODE_EDIT_2;
      edit_mode_section = afg2.section;
      edit_mode_step_num = 0;
      right_counter = SCROLL_WAIT_COUNTER;
      left_counter = SCROLL_WAIT_COUNTER;
      update_display();
    }
  }

  // Section shift for each afg in 16 slider mode
  if (!Is_Expander_Present()) {
    if (!key->b.StepLeft && !key->b.StageAddress1Display) {
      AfgSetSection(AFG1, 0);
      display_mode = DISPLAY_MODE_VIEW_1;
      update_display();
    } else if (!key->b.StepRight && !key->b.StageAddress1Display) {
      AfgSetSection(AFG1, 1);
      display_mode = DISPLAY_MODE_VIEW_1;
      update_display();
    } else if (!key->b.StepLeft && !key->b.StageAddress2Display) {
      AfgSetSection(AFG2, 0);
      display_mode = DISPLAY_MODE_VIEW_2;
      update_display();
    } else if (!key->b.StepRight && !key->b.StageAddress2Display) {
      AfgSetSection(AFG2, 1);
      display_mode = DISPLAY_MODE_VIEW_2;
      update_display();
    }
  }

  // Decrement counters
  if (!key->b.StepLeft && left_counter) {
    // Long hold
    left_counter -= 1;
  } else if (key->b.StepLeft) {
    // Release
    left_counter = SHORT_COUNTER_TICKS;
  }

  if (!key->b.StepRight && right_counter) {
    right_counter -= 1;
  } else if (key->b.StepRight) {
    right_counter = SHORT_COUNTER_TICKS;
  }

  // Left counter expired, do step left
  if (!left_counter) {
    update_display();
    if (edit_mode_step_num == 0) {
      // Wrap around to max step
      edit_mode_step_num = get_max_step();
    } else {
      // Decrement edit step
      edit_mode_step_num -= 1;
    }
    // Long count when held down
    left_counter = LONG_COUNTER_TICKS;
  }

  // Right counter expired, do step right
  if (!right_counter) {
    update_display();
    if (edit_mode_step_num == get_max_step()) {
      // Wrap around to 0
      edit_mode_step_num = 0;
    } else {
      // Increment edit step
      edit_mode_step_num += 1;
    }
    // Long count when held down
    right_counter = LONG_COUNTER_TICKS;
  }
}

void ControllerCheckClear() {
  uButtons myButtons;
  static uint8_t clear_counter1 = 0, clear_counter2 = 0;
  static uint8_t inhibit_modal = 0;

  TIM6->SR = (uint16_t) ~TIM_IT_Update;

  myButtons.value = HC165_ReadSwitches();

  if (clear_counter1 < 30 && clear_counter2 < 30) {
    if (!myButtons.b.ClearUp || !myButtons.b.ClearDown) {
      if (!myButtons.b.ClearUp) {
        clear_counter1++;
      } else {
        clear_counter1 = 0;
      }
      if (!myButtons.b.ClearDown) {
        clear_counter2++;
      } else {
        clear_counter2 = 0;
      }
    } else {
      if (clear_counter1 && !inhibit_modal) {
        // Go into LOAD modal in main loop
        controller_job_flags.modal_loop = CONTROLLER_MODAL_LOAD;
        inhibit_modal = 1;
      } else if (clear_counter2 && !inhibit_modal) {
        // Go into SAVE modal in main loop
        controller_job_flags.modal_loop = CONTROLLER_MODAL_SAVE;
        inhibit_modal = 1;
      }
      // Reset counters
      clear_counter1 = 0;
      clear_counter2 = 0;
      TIM_SetCounter(TIM6, 0x00);
      TIM6->CR1 &= ~TIM_CR1_CEN;
    }
  }
  else if (clear_counter1 >= 30 || clear_counter2 >= 30) {
    // CLEAR
    // Signal by flashing step leds
    RunClearAnimation();

    TIM_SetCounter(TIM6, 0x00);
    TIM6->CR1 &= ~TIM_CR1_CEN;

    if (clear_counter1 == 30 || clear_counter2 == 30) {
      InitProgram();
      AfgHardStop(AFG1);
      AfgHardStop(AFG2);
    };

    clear_counter1 = 0;
    clear_counter2 = 0;
  };

  if (myButtons.b.ClearUp && myButtons.b.ClearDown) {
    // Means clear switch is in the middle
    inhibit_modal = 0;
  }
}

void ControllerCalibrationLoop() {
    uButtons switches;
    switches.value = HC165_ReadSwitches();

    DISPLAY_LED_I_OFF;
    DISPLAY_LED_II_OFF;

    // Run animation, and continue doing adc reads until stage 2 advance is pressed
    while (switches.b.StageAddress2Advance) {
      RunCalibrationAnimation();
      switches.value = HC165_ReadSwitches();

      if (!switches.b.Pulse1On) {
        // Choose normal behavior
        swapped_pulses = 0;
      } else if (!switches.b.Pulse2On) {
        // Choose swapped pulse leds behavior
        swapped_pulses = 1;
      }

      // Pulse-SWITCH swap (independent of the LED swap above), for units whose
      // Pulse 1/2 switch inputs are reversed. Time Source External up = swap,
      // Time Source Internal up = normal.
      if (!switches.b.TimeSourceExternal) {
        swapped_pulse_switches = 1;
      } else if (!switches.b.TimeSourceInternal) {
        swapped_pulse_switches = 0;
      }

      // Shift adc mux if time
      if (controller_job_flags.adc_mux_shift_out) {
        // Disable conversion during shift
        controller_job_flags.inhibit_adc = 1;
        if (Is_Expander_Present()) {
          // Increment the slider, including expander sliders
          controller_job_flags.adc_pot_sel = AdcMuxAdvanceExpanded(controller_job_flags.adc_pot_sel);
        } else {
          // Increments the slider
          controller_job_flags.adc_pot_sel = AdcMuxAdvance(controller_job_flags.adc_pot_sel);
        }
        controller_job_flags.adc_mux_shift_out = 0;
        // Wait for settle
        delay_ms(10);
        // Reenable conversion again
        controller_job_flags.inhibit_adc = 0;
      }
    } // End read loop

    // Read calibration constants
    adc_pause();
    for (uint8_t c = 0; c < 8 ; c++) {
      cal_constants[c] = add_data[c];
      if (cal_constants[c] < 500) {
        // Capture time: a real full-scale (10v / pot maxed) reading is near
        // 4095. Anything this low means the input is unpatched or broken, so
        // store a unity-ish constant rather than a huge scaler.
        // (See the lower <100 guard in PrecomputeCalibration, which catches
        // disconnected/garbage values read back from a blank eprom.)
        cal_constants[c] = 4095;
      }
    };
    // Pack the calibration constants and pulse-led flag into a versioned,
    // checksummed record.
    StoredCal cal;
    for (uint8_t c = 0; c < 8; c++) cal.payload.cal_constants[c] = cal_constants[c];
    cal.payload.swapped_pulses = swapped_pulses;
    cal.payload.swapped_pulse_switches = swapped_pulse_switches;
    marf_stored_cal_finalize(&cal);

    __disable_irq();

    // Erase EPROM
    CAT25512_erase();

    // Store the calibration record to eprom
    CAT25512_write_block(
        eprom_memory.analog_cal_data.start,
        (unsigned char *) &cal,
        eprom_memory.analog_cal_data.size);

    __enable_irq();
    adc_resume();
}

void ControllerLoadCalibration() {
  StoredCal cal;

  // Read the calibration record
  CAT25512_read_block(
      eprom_memory.analog_cal_data.start,
      (unsigned char *) &cal,
      eprom_memory.analog_cal_data.size);

  if (marf_stored_cal_valid(&cal)) {
    for (uint8_t c = 0; c < 8; c++) cal_constants[c] = cal.payload.cal_constants[c];
    swapped_pulses = cal.payload.swapped_pulses;
    swapped_pulse_switches = cal.payload.swapped_pulse_switches;
  } else {
    // Blank, old-format or corrupt calibration: fall back to unity scaling so
    // the module is usable (if uncalibrated) instead of wildly mis-scaled.
    for (uint8_t c = 0; c < 8; c++) cal_constants[c] = 4095;
    swapped_pulses = 0;
    swapped_pulse_switches = 0;
  }

  // Precompute scalers from calibration data
  PrecomputeCalibration();
}


// Load program loop
void ControllerLoadProgramLoop() {
  StoredProgram saved_program = {};
  uint8_t program_num = 0;
  uButtons previous_switches, switches;
  uint32_t switch_last_read_time = 0;
  uint32_t now = 0;
  previous_switches.value = HC165_ReadSwitches();

  mode_led_breathe = 0;   // this loop sends the mode LEDs directly
  StepLedsLightSingleStep(0);

  while (1) {
    now = get_millis();
    ControllerCommonAllLoops();

    if (now - switch_last_read_time > 5) {
      switches.value = HC165_ReadSwitches();
      switch_last_read_time = now;
    } else {
      switches.value = previous_switches.value;
    }

    if (switches.value != previous_switches.value) {
      // Allow stage address to continue to function in modal
      ControllerProcessStageAddressSwitches(&switches);

      if (!switches.b.StepRight) {
        if (program_num >= 15) {
          program_num = 0;
        } else {
          program_num += 1;
        }
        StepLedsLightSingleStep(program_num);
      } else if (!switches.b.StepLeft) {
        if (program_num == 0) {
          program_num = 15;
        } else {
          program_num -= 1;
        }
        StepLedsLightSingleStep(program_num);
      } else if (!switches.b.ClearUp) {
        // Load program.
        // Read from EPROM into temporary saved_program
        CAT25512_read_block(
            eprom_memory.programs[program_num].start,
            (unsigned char *) &saved_program,
            eprom_memory.programs[program_num].size);

        if (marf_stored_program_valid(&saved_program)) {
          // Copy from saved_program to steps and sliders
          memcpy((void *) steps, (void *) saved_program.payload.steps, sizeof(steps));
          memcpy((void *) sliders, (void *) saved_program.payload.sliders, sizeof(sliders));

          // Restore per-sequence scale/root (clamped defensively)
          for (uint8_t a = 0; a < 2; a++) {
            uint8_t sc = saved_program.payload.scale[a];
            uint8_t rt = saved_program.payload.root[a];
            afg_scale[a] = (sc < SCALE_COUNT) ? sc : SCALE_CHROMATIC;
            afg_root[a]  = (rt < 12) ? rt : 0;
          }

          // Pin sliders and reset to step 1
          pin_all_sliders();
          AfgReset(AFG1);
          AfgReset(AFG2);

          // Cute little animation
          RunLoadProgramAnimation();
        } else {
          // Empty, old-format or corrupt slot: leave the running program
          // untouched and signal that nothing was loaded.
          RunErrorAnimation();
        }
        display_mode = DISPLAY_MODE_VIEW_1;
        controller_job_flags.modal_loop = CONTROLLER_MODAL_NONE;
        return;
      } else if (!switches.b.StageAddress1Display || !switches.b.StageAddress2Display) {
        // Abort load
        display_mode = DISPLAY_MODE_VIEW_1;
        controller_job_flags.modal_loop = CONTROLLER_MODAL_NONE;
        return;
      }
      previous_switches.value = switches.value;
    } else {
      RunWaitingLoadSaveAnimation(AfgGetControllerState(AFG1), AfgGetControllerState(AFG2));
      delay_us(500);
    }
  }
}

// Save program loop
void ControllerSaveProgramLoop() {
  uint8_t program_num = 0;
  StoredProgram saved_program = {};
  uButtons previous_switches, switches;
  uint32_t switch_last_read_time = 0;
  uint32_t now = 0;
  previous_switches.value = HC165_ReadSwitches();

  mode_led_breathe = 0;   // this loop sends the mode LEDs directly
  StepLedsLightSingleStep(0);

  while (1) {
    now = get_millis();
    ControllerCommonAllLoops();

    if (now - switch_last_read_time > 5) {
      switches.value = HC165_ReadSwitches();
      switch_last_read_time = now;
    } else {
      switches.value = previous_switches.value;
    }

    if (switches.value != previous_switches.value) {
      // Allow stage address to continue to function in modal
      ControllerProcessStageAddressSwitches(&switches);

      if (!switches.b.StepRight) {
        if (program_num >= 15) {
          program_num = 0;
        } else {
          program_num += 1;
        }
        StepLedsLightSingleStep(program_num);
      } else if (!switches.b.StepLeft) {
        if (program_num == 0) {
          program_num = 15;
        } else {
          program_num -= 1;
        }
        StepLedsLightSingleStep(program_num);
      } else if (!switches.b.ClearDown) {
        // Save program
        // Copy from steps and sliders to temp saved_program.
        // Prevent updates to slider values during this time.
        controller_job_flags.inhibit_adc = 1;
        memcpy((void *) saved_program.payload.steps, (void *) steps, sizeof(steps));
        memcpy((void *) saved_program.payload.sliders, (void *) sliders, sizeof(sliders));
        controller_job_flags.inhibit_adc = 0;

        // Capture per-sequence scale/root
        for (uint8_t a = 0; a < 2; a++) {
          saved_program.payload.scale[a] = afg_scale[a];
          saved_program.payload.root[a]  = afg_root[a];
        }

        // Stamp magic/version/CRC, then write the record to the EPROM
        marf_stored_program_finalize(&saved_program);
        CAT25512_write_block(
            eprom_memory.programs[program_num].start,
            (unsigned char *) &saved_program,
            eprom_memory.programs[program_num].size);

        // Run cute animation
        RunSaveProgramAnimation();
        display_mode = DISPLAY_MODE_VIEW_1;
        controller_job_flags.modal_loop = CONTROLLER_MODAL_NONE;
        return;
      } else if (!switches.b.StageAddress1Display || !switches.b.StageAddress2Display) {
        // Abort save
        display_mode = DISPLAY_MODE_VIEW_1;
        controller_job_flags.modal_loop = CONTROLLER_MODAL_NONE;
        return;
      }
      previous_switches.value = switches.value;
    } else {
      RunWaitingLoadSaveAnimation(AfgGetControllerState(AFG1), AfgGetControllerState(AFG2));
      delay_us(500);
    }
  }
}

// Immediately scan all the adc2 lines, and then process mode changes from the input pulses.
// This guarantees that we acquire the new values present on external inputs and stage address
// before we attempt to utilize them. This prevents a couple ms of glitching on state transitions.
// New readings are double buffered and applied at the end so that the current state being held
// also does not glitch.
void ControllerScanAdcLoop() {
  // Double buffer the new adc readings and apply them all together
  uint16_t new_readings[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  adc_pause();
  controller_job_flags.inhibit_adc = 1;
  AdcMuxResetAllOff();

  // Scan all of the ADC2 channels
  for (uint8_t i = 0; i < 8; i++) {
    AdcMuxSelectAdc2(i);  // Shift the mux
    delay_us(10);         // Settling time

    // Start injected conversion
    ADC_SoftwareStartInjectedConv(ADC2);
    while (ADC_GetFlagStatus(ADC2, ADC_FLAG_JEOC) == RESET) {}
    new_readings[i] = ADC_GetInjectedConversionValue(ADC2, ADC_InjectedChannel_1);
    ADC_ClearFlag(ADC2, ADC_FLAG_JEOC);
  }

  // Apply all the new readings immediately without filtering
  for (uint8_t i = 0; i < 8; i++) {
    WriteOtherCvWithoutSmoothing(i, new_readings[i]);
  }

  // Now process the pending events
  // Disable all irq including the function generators
  __disable_irq();
  if (any_pulses_high(controller_job_flags.afg1_interrupts)) {
    AfgProcessModeChanges(AFG1, controller_job_flags.afg1_interrupts);
    controller_job_flags.afg1_interrupts = PULSE_INPUTS_NONE;
  }
  if (any_pulses_high(controller_job_flags.afg2_interrupts)) {
    AfgProcessModeChanges(AFG2, controller_job_flags.afg2_interrupts);
    controller_job_flags.afg2_interrupts = PULSE_INPUTS_NONE;
  }

  // Restore normal operation
  AdcMuxReset();
  delay_us(10);
  controller_job_flags.adc_pot_sel = 0;
  controller_job_flags.adc_mux_shift_out = 1;
  controller_job_flags.inhibit_adc = 1;
  controller_job_flags.modal_loop = CONTROLLER_MODAL_NONE;
  __enable_irq();
  adc_resume();
}

