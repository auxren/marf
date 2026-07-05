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
#include "presets.h"
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
volatile uint8_t scale_select_value = 0;   // value to show on the step LEDs
// When set, scale_select_value is shown in binary across the 16 step LEDs (read
// as hex nibbles) instead of a single lit LED, so scale numbers past 16 fit.
volatile uint8_t scale_select_binary = 0;
// When set, scale_select_value is shown as a filled bar (LEDs 0..value lit) --
// used by the per-step pulse-width gesture.
volatile uint8_t scale_select_bar = 0;

// On units whose Pulse 1/2 channels are physically reversed, the panel programs
// and outputs the wrong pulse. Set during calibration and stored in the cal
// record; when set, both the Pulse 1/2 switch inputs (controller.c) and the
// Pulse 1/2 output jacks (main.c) are swapped so the panel matches.
volatile uint8_t swapped_pulse_switches = 0;

// SWD-visible debug taps for the switch-scan pipeline (diagnosing the chord
// dropout while externally clocked). Cheap; safe to leave in.
volatile uint32_t dbg_scan_count = 0;     // 5 ms HC165 scans performed
volatile uint32_t dbg_accept_count = 0;   // debounce acceptances
volatile uint64_t dbg_new_switches = 0;   // latest raw HC165 read
volatile uint64_t dbg_stable_switches = 0;// latest debounced state
// Latching press-evidence counters: total scans that saw these switches
// pressed in the RAW read, ever since boot. Timing-free diagnosis: press
// whenever, read later.
volatile uint32_t dbg_quantize_scans = 0; // scans with Quantize read pressed
volatile uint32_t dbg_srcext_scans = 0;   // scans with Source External pressed
volatile uint32_t dbg_chord_scans = 0;    // scans with both pressed

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

  // Latched while the pulse-width chord is engaged (until both switches release).
  uint8_t pulse_width_engaged = 0;

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
      dbg_scan_count++;
      dbg_new_switches = new_switches_state;
      dbg_stable_switches = stable_switches_state;
      {
        uButtons raw;
        raw.value = new_switches_state;
        uint8_t q = !raw.b.OutputQuantize, s = !raw.b.SourceExternal;
        if (q) dbg_quantize_scans++;
        if (s) dbg_srcext_scans++;
        if (q && s) dbg_chord_scans++;
      }
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
      dbg_accept_count++;
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
    // eg when the clock is running fast. (Suppressed while the Turing chord or
    // the pulse-width chord is held, so those gestures don't reprogram steps --
    // important while running, where the "current step" advances and would
    // otherwise be stamped on every step that plays during the hold.)
    uint8_t turing_chord = !switches.b.OutputQuantize && !switches.b.SourceExternal;
    uint8_t pulse_width_chord = !switches.b.TimeSourceExternal && !switches.b.TimeRange1
                                && switches.b.OutputQuantize && switches.b.SourceExternal;
    // Once the pulse-width chord is seen, stay "engaged" (suppressing programming)
    // until BOTH chord switches are released. They are momentary, so one can slip
    // during the hold; without this latch the still-held .03/Time-Source switch
    // would stamp every step that plays past while running.
    uint8_t pw_either_held = !switches.b.TimeSourceExternal || !switches.b.TimeRange1;
    if (pulse_width_chord) pulse_width_engaged = 1;
    else if (!pw_either_held) pulse_width_engaged = 0;
    if (!turing_chord && !pulse_width_engaged) {
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

    // Hold Time Source (External) + move a time slider sets that step's pulse width.
    ControllerProcessPulseWidth(&switches);

    // Holding both Stage Address Reset buttons for >1s randomizes the program.
    ControllerProcessRandomize(&switches);

    // Safety net: if no slider gesture is active, never leave slider commits
    // frozen (guards against a gesture being interrupted mid-hold and leaving
    // the sliders unresponsive).
    if (!scale_select_active) scale_select_freeze = 0;

    // Update panel state
    UpdateModeSectionLeds(AfgGetControllerState(AFG1), AfgGetControllerState(AFG2), edit_mode_section, edit_mode_step_num);
    display_update_flags.b.MainDisplay = 0;
    UpdateStepSectionLeds(AfgGetControllerState(AFG1), AfgGetControllerState(AFG2), edit_mode_step_num);
    display_update_flags.b.StepsDisplay = 0;

    // While selecting a scale/root, show the value on the step LEDs instead
    // (this overrides the normal step display until the gesture is released).
    // Scale numbers can exceed 16, so they are shown in binary across the 16
    // step LEDs (read as hex nibbles); roots and Turing values stay single-LED.
    if (scale_select_active) {
      if (scale_select_bar) {
        // Pulse-width gesture: filled bar, LEDs 0..value lit. Clamp to 15 so the
        // shift is never >= 32.
        uint8_t bar = scale_select_value > 15 ? 15 : scale_select_value;
        steps_leds_lit = 0xFFFFFFFFu & ~(((1UL << (bar + 1)) - 1UL));
      } else if (scale_select_binary) {
        steps_leds_lit = 0xFFFFFFFFu & ~((uint32_t) scale_select_value);
      } else {
        steps_leds_lit = 0xFFFFFFFFu & ~(1UL << scale_select_value);
      }
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
  static uint16_t last_v[32], last_t[32];  // last-loop positions (moving now?)
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
        last_v[i] = slider_raw_v[i];
        last_t[i] = slider_raw_t[i];
        moved_v[i] = 0;
        moved_t[i] = 0;
      }
    }

    // A voltage slider moved past the threshold picks the scale; a time slider
    // picks the root. Since all sliders write the SAME per-sequence value, a
    // slider only writes while it is actually moving - otherwise a slider
    // displaced earlier in the hold would keep re-asserting its value every
    // loop and mask anything selected on another slider afterwards.
    for (uint8_t i = 0; i <= max; i++) {
      int dv = (int) slider_raw_v[i] - (int) snap_v[i];   // moved since entry?
      int dt = (int) slider_raw_t[i] - (int) snap_t[i];
      if (dv < 0) dv = -dv;
      if (dt < 0) dt = -dt;
      int dlv = (int) slider_raw_v[i] - (int) last_v[i];  // moving right now?
      int dlt = (int) slider_raw_t[i] - (int) last_t[i];
      if (dlv < 0) dlv = -dlv;
      if (dlt < 0) dlt = -dlt;
      last_v[i] = slider_raw_v[i];
      last_t[i] = slider_raw_t[i];
      if (dv > SCALE_SELECT_MOVE_THRESHOLD && dlv > 8) {
        afg_scale[afg] = scale_from_slider(slider_raw_v[i]);
        showing_root = 0;
        moved_v[i] = 1;
      }
      if (dt > SCALE_SELECT_MOVE_THRESHOLD && dlt > 8) {
        afg_root[afg] = root_from_slider(slider_raw_t[i]);
        showing_root = 1;
        moved_t[i] = 1;
      }
    }

    if (showing_root) {
      scale_select_value = afg_root[afg];          // 0..11, single LED
      scale_select_binary = 0;
    } else {
      scale_select_value = afg_scale[afg] + 1;      // 1-based scale number, in hex/binary
      scale_select_binary = 1;
    }
    scale_select_bar = 0;
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
// The hold timer tolerates brief (<150 ms) dropouts in the switch reading, so
// a momentary glitch (e.g. noise while heavily externally clocked) can't
// silently restart the 0.8 s requirement.
void ControllerProcessTuring(uButtons * key) {
  static uint8_t state = 0;        // 0 idle, 1 timing, 2 toggled (await release)
  static uint32_t start = 0;
  static uint32_t last_held = 0;   // last time the chord read as held

  uint8_t both_held = !key->b.OutputQuantize && !key->b.SourceExternal;
  uint32_t now = get_millis();

  if (both_held) {
    last_held = now;
    if (state == 0) { state = 1; start = now; }
    else if (state == 1 && (now - start) > 800) {
      uint8_t afg = (display_mode == DISPLAY_MODE_VIEW_2 ||
                     display_mode == DISPLAY_MODE_EDIT_2) ? AFG2 : AFG1;
      turing_enabled[afg] ^= 1;
      mode_led_breathe = 0;            // avoid PWM contention during the animation
      if (turing_enabled[afg]) {
        RunTuringEnterAnimation();     // forward chase: mode ON
      } else {
        RunTuringExitAnimation();      // reverse chase: mode OFF
      }
      state = 2;
    }
  } else if (state == 1 && (now - last_held) < 150) {
    // Brief dropout while timing: keep the hold alive.
  } else {
    state = 0;
  }
}

// Holding BOTH Stage Address Reset buttons for >1 s randomizes the stage block
// of the currently displayed AFG (slider values, voltage ranges, quantize/slope/
// pulses, time ranges and a random loop length) and plays a ~2 s "randomizing"
// light show. Only the displayed AFG's stages are touched, so AFG 1 (stages
// 1-16) and AFG 2 (stages 17-32) can each be randomized independently -- switch
// the display to the AFG you want to roll, then hold both resets.
void ControllerProcessRandomize(uButtons * key) {
  static uint8_t state = 0;        // 0 idle, 1 timing, 2 fired (await release)
  static uint32_t start = 0;
  static uint32_t last_held = 0;

  uint8_t both_reset = !key->b.StageAddress1Reset && !key->b.StageAddress2Reset;
  uint32_t now = get_millis();

  if (both_reset) {
    last_held = now;
    if (state == 0) { state = 1; start = now; }
    else if (state == 1 && (now - start) > 1000) {
      // Randomize the block the displayed AFG is showing (its current section).
      uint8_t disp_afg = (display_mode == DISPLAY_MODE_VIEW_2 ||
                          display_mode == DISPLAY_MODE_EDIT_2) ? AFG2 : AFG1;
      uint8_t section = AfgGetControllerState(disp_afg).section;

      turing_seed(DWT->CYCCNT);     // fresh entropy from the cycle counter
      RandomizeProgram(section);
      RunRandomizeAnimation();
      AfgReset(disp_afg);           // restart just that sequence from its step 1
      state = 2;
    }
  } else if (state == 1 && (now - last_held) < 150) {
    // Brief dropout while timing: keep the hold alive.
  } else {
    state = 0;
  }
}

// Clock the per-stage shift registers from the four external inputs.
#if MARF_HW == 1

// v1 (frozen): a rising edge on external input k clocks EVERY stage assigned
// to clock k, with the stage's committed voltage slider as the "big knob".
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

#else

// v2: a rising edge on external input k clocks ONLY the stage(s) the
// sequencers are currently ON (if assigned to clock k) - "when on a step, the
// machine steps at that clock". A locked stage's register therefore never
// moves while other stages play, so a sequence of locked stages repeats
// exactly, pass after pass.
//
// Soft-normalling: a stage whose assigned input has NO CV present instead
// steps its register once each time the sequencer ENTERS it - so the mode
// works with nothing patched, and plugging a clock into A-D takes over.
//
// Edge detection uses a low threshold with hysteresis (~2 V rising, ~1 V
// falling on a 10 V input) so ordinary 5 V clocks register reliably - the old
// mid-scale (~5 V) threshold sat exactly on a 5 V gate and missed it.
#define TURING_CLK_HI 820    // ~2 V of 10 V full scale
#define TURING_CLK_LO 410    // ~1 V - must fall below to re-arm

// Step the given stage's register (slip odds from its LIVE voltage slider -
// not the committed/pinnable value, which went dead after loads/gestures).
static void turing_step_stage(uint8_t s) {
  uStep st = get_step_programming(0, s);
  turing_set_length(&turing_machines[s], st.b.TuringLength + 2);  // 2..16
  // On a 16-slider unit a section-1 stage (16-31) sits on physical slider s-16.
  uint8_t slider = Is_Expander_Present() ? s : (uint8_t) (s & 0x0F);
  turing_clock(&turing_machines[s], slider_raw_v[slider]);
}

void ControllerProcessTuringClocks(void) {
  static uint8_t high[4] = { 0, 0, 0, 0 };
  static uint8_t prev_cur[2] = { 0xFF, 0xFF };   // stage entry detection

  if (!turing_enabled[0] && !turing_enabled[1]) {
    prev_cur[0] = prev_cur[1] = 0xFF;
    return;
  }

  AfgControllerState a1 = AfgGetControllerState(AFG1);
  AfgControllerState a2 = AfgGetControllerState(AFG2);
  uint8_t cur[2];
  cur[AFG1] = (uint8_t) (a1.step_num + (a1.section << 4));
  cur[AFG2] = (uint8_t) (a2.step_num + (a2.section << 4));

  // External clock edges: step the current stage(s) assigned to input k.
  for (uint8_t k = 0; k < 4; k++) {
    uint16_t v = add_data[k];
    uint8_t rising = 0;
    if (!high[k] && v >= TURING_CLK_HI) { high[k] = 1; rising = 1; }
    else if (high[k] && v < TURING_CLK_LO) { high[k] = 0; }
    if (!rising) continue;

    uint8_t did1 = 0;
    if (turing_enabled[AFG1] && get_step_programming(0, cur[AFG1]).b.TuringClock == k) {
      turing_step_stage(cur[AFG1]);
      did1 = 1;
    }
    // Don't double-clock a stage both sequencers are sitting on.
    if (turing_enabled[AFG2] && !(did1 && cur[AFG2] == cur[AFG1]) &&
        get_step_programming(0, cur[AFG2]).b.TuringClock == k) {
      turing_step_stage(cur[AFG2]);
    }
  }

  // Soft-normalled stage-entry clocking for stages whose assigned input is
  // unpatched.
  uint8_t entered1 = 0;
  for (uint8_t a = 0; a < 2; a++) {
    if (!turing_enabled[a]) { prev_cur[a] = 0xFF; continue; }
    if (cur[a] == prev_cur[a]) continue;
    prev_cur[a] = cur[a];
    // Both sequencers entering the same stage together: step it once.
    if (a == AFG2 && entered1 && cur[AFG2] == cur[AFG1]) continue;
    if (a == AFG1) entered1 = 1;
    uStep st = get_step_programming(0, cur[a]);
    if (!external_present[st.b.TuringClock]) {
      turing_step_stage(cur[a]);
    }
  }
}

#endif  // MARF_HW

// Per-stage Turing config gesture (only in Turing mode for the displayed
// sequence). Holding Source-External (without Quantize) and moving a voltage
// slider sets which of the four external jacks (0-3) clocks that stage; moving a
// time slider sets that stage's loop length (2-16). The step LEDs show the value.
// Sliders are frozen during the gesture (so the committed step voltage/duration
// don't change) and the moved ones are pinned on release. Note the slip amount
// follows the LIVE slider position, so wherever a voltage slider lands after
// the gesture is that stage's new slip setting - position is truth.
void ControllerProcessTuringConfig(uButtons * key) {
  static uint8_t active = 0;
  static uint16_t snap_v[32], snap_t[32];
  static uint16_t last_v[32], last_t[32];   // last-loop positions: spot the slider moving NOW
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
      // Seed the LED display with the focused stage's CURRENT clock input, so
      // entering the gesture shows the real assignment. Previously it showed a
      // stale value from an earlier gesture until a slider crossed the move
      // threshold - entering with the slider already at the bottom read as
      // "bottom = B" while nothing had actually been written.
      {
        AfgControllerState a1 = AfgGetControllerState(AFG1);
        AfgControllerState a2 = AfgGetControllerState(AFG2);
        uint8_t f_step, f_section;
        if (display_mode == DISPLAY_MODE_VIEW_1) {
          f_step = a1.step_num; f_section = a1.section;
        } else if (display_mode == DISPLAY_MODE_VIEW_2) {
          f_step = a2.step_num; f_section = a2.section;
        } else {
          f_step = edit_mode_step_num; f_section = edit_mode_section;
        }
        scale_select_value = get_step_programming(f_section, f_step).b.TuringClock;
      }
      for (uint8_t i = 0; i <= max; i++) {
        snap_v[i] = slider_raw_v[i];
        snap_t[i] = slider_raw_t[i];
        last_v[i] = slider_raw_v[i];
        last_t[i] = slider_raw_t[i];
        moved_v[i] = 0;
        moved_t[i] = 0;
      }
    }
    for (uint8_t i = 0; i <= max; i++) {
      int dv = (int) slider_raw_v[i] - (int) snap_v[i];   // moved since entry?
      int dt = (int) slider_raw_t[i] - (int) snap_t[i];
      if (dv < 0) dv = -dv;
      if (dt < 0) dt = -dt;
      int dlv = (int) slider_raw_v[i] - (int) last_v[i];  // moving right now?
      int dlt = (int) slider_raw_t[i] - (int) last_t[i];
      if (dlv < 0) dlv = -dlv;
      if (dlt < 0) dlt = -dlt;
      last_v[i] = slider_raw_v[i];
      last_t[i] = slider_raw_t[i];
      if (dv > SCALE_SELECT_MOVE_THRESHOLD) {
        uint8_t clk = (uint8_t) (((uint32_t) slider_raw_v[i] * 4) / 4096);
        if (clk > 3) clk = 3;
        steps[i].b.TuringClock = clk;
        // The LED display follows the slider being moved NOW - a slider that
        // was displaced earlier in the hold (especially a higher-numbered one)
        // must not mask changes made on another slider afterwards.
        if (dlv > 8) scale_select_value = clk;
        moved_v[i] = 1;
      }
      if (dt > SCALE_SELECT_MOVE_THRESHOLD) {
        uint8_t len = (uint8_t) (((uint32_t) slider_raw_t[i] * 15) / 4096);  // 0..14
        if (len > 14) len = 14;
        steps[i].b.TuringLength = len;
        if (dlt > 8) scale_select_value = (uint8_t) (len + 1);  // show length-1 (1..15)
        moved_t[i] = 1;
      }
    }
    scale_select_binary = 0;   // Turing values are small -> single LED
    scale_select_bar = 0;
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

// Per-step pulse-width gesture. The chord is Time Source UP (External) + the
// .03 Time Range switch (Time Range 1) UP, with Quantize and Source-External
// NOT held. While held, moving a time slider sets that step's pulse/gate length
// (0..15 -> ~1%..99% of the step); the step LEDs show a filled bar of the value.
//
// The held chord switches would otherwise program the focused step's Time Source
// and Time Range. To make the chord fully non-destructive, the focused step's
// clean Time Source + Time Range are captured every loop *while neither chord
// switch is held*, and restored once the chord is engaged -- and kept restored
// each loop until BOTH chord switches are released (so a still-held switch on the
// way out can't re-program it). Using either switch on its own (not the chord)
// still programs normally.
void ControllerProcessPulseWidth(uButtons * key) {
  static uint8_t active = 0;
  static uint8_t pending_restore = 0;
  static uint16_t snap_t[32];
  static uint16_t last_t[32];         // last-loop positions, to spot the moving one
  static uint8_t moved_t[32];
  static uint8_t clean_idx = 0;
  static uStep   clean_step;          // focused step's clean Time Source/Range

  uint8_t max = get_max_step();
  uint8_t ts_ext_held = !key->b.TimeSourceExternal;
  uint8_t tr03_held   = !key->b.TimeRange1;          // the ".03" time range switch
  uint8_t either_held = ts_ext_held || tr03_held;
  uint8_t chord = ts_ext_held && tr03_held &&
                  key->b.OutputQuantize && key->b.SourceExternal;

  // Currently focused step.
  AfgControllerState a1 = AfgGetControllerState(AFG1);
  AfgControllerState a2 = AfgGetControllerState(AFG2);
  uint8_t focus_idx;
  if (display_mode == DISPLAY_MODE_VIEW_1) {
    focus_idx = a1.step_num + (a1.section << 4);
  } else if (display_mode == DISPLAY_MODE_VIEW_2) {
    focus_idx = a2.step_num + (a2.section << 4);
  } else {
    focus_idx = edit_mode_step_num + (edit_mode_section << 4);
  }

  if (chord) {
    if (!active) {
      active = 1;
      scale_select_freeze = 1;
      scale_select_value = steps[focus_idx].b.PulseWidth;  // seed the bar
      for (uint8_t i = 0; i <= max; i++) {
        snap_t[i] = slider_raw_t[i];
        last_t[i] = slider_raw_t[i];
        moved_t[i] = 0;
      }
    }
    for (uint8_t i = 0; i <= max; i++) {
      int dt = (int) slider_raw_t[i] - (int) snap_t[i];   // moved since entry?
      if (dt < 0) dt = -dt;
      int dl = (int) slider_raw_t[i] - (int) last_t[i];   // moving right now?
      if (dl < 0) dl = -dl;
      last_t[i] = slider_raw_t[i];
      if (dt > SCALE_SELECT_MOVE_THRESHOLD) {
        uint8_t pw = (uint8_t) (((uint32_t) slider_raw_t[i] * 16u) / 4096u);
        if (pw > 15) pw = 15;
        steps[i].b.PulseWidth = pw;
        moved_t[i] = 1;
        // The bar follows the slider being moved *now*, so adjusting a different
        // stage isn't masked by a higher-index slider moved earlier.
        if (dl > 8) scale_select_value = pw;
      }
    }
    scale_select_binary = 0;
    scale_select_bar = 1;
    scale_select_active = 1;
    pending_restore = 1;               // restore the step's time src/range on exit
  } else {
    if (active) {
      // Chord just released: pin the moved time sliders so their step times
      // don't jump to the width position.
      scale_select_freeze = 0;
      for (uint8_t i = 0; i <= max; i++) {
        if (moved_t[i]) {
          time_slider_pins.high |= (1UL << i);
          time_slider_pins.low  |= (1UL << i);
        }
      }
      active = 0;
      scale_select_active = 0;
    }
    if (pending_restore) {
      // Resume the captured clean Time Source + Time Range, every loop until both
      // chord switches are released (a lingering held switch would re-program it).
      volatile uStep *s = &steps[clean_idx];
      s->b.TimeSource    = clean_step.b.TimeSource;
      s->b.TimeRange_p03 = clean_step.b.TimeRange_p03;
      s->b.TimeRange_p3  = clean_step.b.TimeRange_p3;
      s->b.TimeRange_3   = clean_step.b.TimeRange_3;
      s->b.TimeRange_30  = clean_step.b.TimeRange_30;
      if (!either_held) pending_restore = 0;
    } else if (!either_held) {
      // Neither chord switch held: capture the clean (pre-chord) state.
      clean_idx = focus_idx;
      clean_step = steps[focus_idx];
    }
  }
}

void ControllerApplyProgrammingSwitches(uButtons * key) {
  AfgControllerState afg1 = AfgGetControllerState(AFG1);
  AfgControllerState afg2 = AfgGetControllerState(AFG2);
  uint8_t step_num = 0, section = 0;

  // On units with reversed Pulse 1/2 switch wiring (selected in calibration),
  // swap the Pulse 1/2 switch inputs so the panel programs the intended output.
  uButtons k = *key;
  if (swapped_pulse_switches) {
    k.b.Pulse1On  = key->b.Pulse2On;
    k.b.Pulse1Off = key->b.Pulse2Off;
    k.b.Pulse2On  = key->b.Pulse1On;
    k.b.Pulse2Off = key->b.Pulse1Off;
  }

#if MARF_HW != 1
  // Both Display switches held: "both channels" programming. The switches
  // apply to BOTH generators' current stages, wherever each one is, instead
  // of just the displayed one. (Sliders need no special handling: the two
  // sections share the physical slider bank, so a slider always feeds both.)
  if (!key->b.StageAddress1Display && !key->b.StageAddress2Display) {
    ApplyProgrammingSwitches(afg1.section, afg1.step_num, &k);
    ApplyProgrammingSwitches(afg2.section, afg2.step_num, &k);
    return;
  }
#endif

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

  ApplyProgrammingSwitches(section, step_num, &k);
}

void ControllerProcessStageAddressSwitches(uButtons * key) {
  PulseInputs signals1 = {}, signals2 = {};

  // Only do one of reset, strobe or advance.
  // Panel events pass stamp 0 (manual): they never affect the clock lock.
  if (!key->b.StageAddress1Reset) {
    AfgReset(AFG1);
    update_display();
  } else if (!key->b.StageAddress1PulseSelect) {
    signals1.strobe = 1;
    AfgProcessModeChanges(AFG1, signals1, 0);
  } else if (!key->b.StageAddress1Advance) {
    signals1.start = 1;
    signals1.stop = 1;
    AfgProcessModeChanges(AFG1, signals1, 0);
  }

  if (!key->b.StageAddress2Reset) {
    AfgReset(AFG2);
    update_display();
  } else if ( (!key->b.StageAddress2PulseSelect)) {
    signals2.strobe = 1;
    AfgProcessModeChanges(AFG2, signals2, 0);
  } else if (!key->b.StageAddress2Advance) {
    signals2.start = 1;
    signals2.stop = 1;
    AfgProcessModeChanges(AFG2, signals2, 0);
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

#if MARF_HW != 1
  // Both Display switches held = "both channels" chord: programming switches
  // apply to both generators (see ControllerApplyProgrammingSwitches), Stage
  // No shifts both sections, and the display-mode flips / edit-entry are
  // suppressed for the duration of the hold.
  uint8_t both_displays = !key->b.StageAddress1Display && !key->b.StageAddress2Display;
#else
  const uint8_t both_displays = 0;   // v1 build: feature disabled (frozen)
#endif

  // Display/edit mode changes
  if (!key->b.StageAddress1Display && !both_displays && display_mode != DISPLAY_MODE_VIEW_1) {
    display_mode = DISPLAY_MODE_VIEW_1;
  }
  if (!key->b.StageAddress2Display && !both_displays && display_mode != DISPLAY_MODE_VIEW_2) {
    display_mode = DISPLAY_MODE_VIEW_2;
  }
  if (!both_displays && (!key->b.StepLeft || !key->b.StepRight)) {
    // Entering edit from view with a Stage No press. Start the edit cursor at
    // the stage currently in view (not always stage 1) and use the normal SHORT
    // debounce -- NOT the long SCROLL_WAIT -- so the press that enters edit
    // registers like any other press. Previously the 120-tick scroll-wait
    // swallowed the first Stage No press after a Display change.
    if (display_mode == DISPLAY_MODE_VIEW_1) {
      display_mode = DISPLAY_MODE_EDIT_1;
      edit_mode_section = afg1.section;
      edit_mode_step_num = afg1.step_num;
      right_counter = SHORT_COUNTER_TICKS;
      left_counter = SHORT_COUNTER_TICKS;
      update_display();
    }
    else if (display_mode == DISPLAY_MODE_VIEW_2) {
      display_mode = DISPLAY_MODE_EDIT_2;
      edit_mode_section = afg2.section;
      edit_mode_step_num = afg2.step_num;
      right_counter = SHORT_COUNTER_TICKS;
      left_counter = SHORT_COUNTER_TICKS;
      update_display();
    }
  }

  // Section shift for each afg in 16 slider mode
  if (!Is_Expander_Present()) {
    if (both_displays && !key->b.StepLeft) {
      // Both channels: shift both generators to steps 1-16
      AfgSetSection(AFG1, 0);
      AfgSetSection(AFG2, 0);
      update_display();
    } else if (both_displays && !key->b.StepRight) {
      // Both channels: shift both generators to steps 17-32
      AfgSetSection(AFG1, 1);
      AfgSetSection(AFG2, 1);
      update_display();
    } else if (!key->b.StepLeft && !key->b.StageAddress1Display) {
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

// Advance the ADC mux one step (used while scanning during calibration).
static void cal_scan_step(void) {
  if (controller_job_flags.adc_mux_shift_out) {
    controller_job_flags.inhibit_adc = 1;
    if (Is_Expander_Present()) {
      controller_job_flags.adc_pot_sel = AdcMuxAdvanceExpanded(controller_job_flags.adc_pot_sel);
    } else {
      controller_job_flags.adc_pot_sel = AdcMuxAdvance(controller_job_flags.adc_pot_sel);
    }
    controller_job_flags.adc_mux_shift_out = 0;
    delay_ms(10);
    controller_job_flags.inhibit_adc = 0;
  }
}

// Two-pass calibration. Pass 1 (everything at max, 10 V on the inputs): press
// Stage Address 1 Advance to capture the high point. Pass 2 (everything at min,
// 0 V on the inputs): press Stage Address 2 Advance to capture the low point and
// save. Captures per-slider and per-input min/max for offset+gain correction,
// plus the legacy gain-only record so old behaviour survives if the new record
// is ever lost. The pulse LED/channel swap selections happen during pass 1.
void ControllerCalibrationLoop() {
    uButtons switches;
    uint16_t v_max[32], v_min[32], t_max[32], t_min[32];
    uint16_t adc2_max[8], adc2_min[8];
    uint8_t max_captured = 0;
    uint8_t afg1_released = 0;     // Advance 1 was held to enter; require release

    ClearSliderCalibration();      // capture RAW slider readings during cal
    DISPLAY_LED_I_OFF;
    DISPLAY_LED_II_OFF;

    switches.value = HC165_ReadSwitches();

    while (1) {
      RunCalibrationAnimation();
      switches.value = HC165_ReadSwitches();

      // Pass indicator: Display I during max pass, Display II during min pass.
      if (max_captured) { DISPLAY_LED_I_OFF; DISPLAY_LED_II_ON; }
      else              { DISPLAY_LED_I_ON;  DISPLAY_LED_II_OFF; }

      // Two INDEPENDENT pulse swaps, because boards fail in different ways:
      //  * LED-only swap -- just the Pulse 1/2 LEDs are reversed (switches and
      //    output jacks are fine). Select with the OUTPUT PULSE switches:
      //    Pulse 2 up = swap LEDs, Pulse 1 up = normal.
      //  * Channel swap -- the switch inputs AND output jacks (and their LEDs)
      //    are reversed. Select with the Time Source switch: External up = swap,
      //    Internal up = normal.
      // Keeping them separate lets a board that only needs one fix not get the
      // other applied on top of an already-correct chain.
      if (!switches.b.Pulse1On) swapped_pulses = 0;
      else if (!switches.b.Pulse2On) swapped_pulses = 1;
      if (!switches.b.TimeSourceExternal) swapped_pulse_switches = 1;
      else if (!switches.b.TimeSourceInternal) swapped_pulse_switches = 0;

      cal_scan_step();

      // Stage Address 1 Advance captures the MAX point (after the boot-hold is
      // released and re-pressed).
      if (switches.b.StageAddress1Advance) {
        afg1_released = 1;
      } else if (afg1_released && !max_captured) {
        adc_pause();
        for (uint8_t i = 0; i < 32; i++) { v_max[i] = slider_raw_v[i]; t_max[i] = slider_raw_t[i]; }
        for (uint8_t c = 0; c < 8; c++) adc2_max[c] = add_data[c];
        adc_resume();
        max_captured = 1;
      }

      // Stage Address 2 Advance captures the MIN point and finishes.
      if (!switches.b.StageAddress2Advance && max_captured) break;
    }

    // Capture the MIN point.
    adc_pause();
    for (uint8_t i = 0; i < 32; i++) { v_min[i] = slider_raw_v[i]; t_min[i] = slider_raw_t[i]; }
    for (uint8_t c = 0; c < 8; c++) adc2_min[c] = add_data[c];

    // Legacy gain-only record: the max readings of the 8 input/knob channels.
    for (uint8_t c = 0; c < 8 ; c++) {
      cal_constants[c] = adc2_max[c];
      if (cal_constants[c] < 500) cal_constants[c] = 4095;
    }
    StoredCal cal;
    for (uint8_t c = 0; c < 8; c++) cal.payload.cal_constants[c] = cal_constants[c];
    cal.payload.swapped_pulses = swapped_pulses;
    cal.payload.swapped_pulse_switches = swapped_pulse_switches;
    marf_stored_cal_finalize(&cal);

    // Two-point record (per-slider + per-input min/max).
    StoredTwoPointCal tp;
    for (uint8_t i = 0; i < 32; i++) {
      tp.payload.v_min[i] = v_min[i]; tp.payload.v_max[i] = v_max[i];
      tp.payload.t_min[i] = t_min[i]; tp.payload.t_max[i] = t_max[i];
    }
    for (uint8_t c = 0; c < 8; c++) {
      tp.payload.adc2_min[c] = adc2_min[c]; tp.payload.adc2_max[c] = adc2_max[c];
    }
    marf_stored_twopoint_finalize(&tp);

    __disable_irq();
    CAT25512_erase();
    CAT25512_write_block(eprom_memory.analog_cal_data.start,
        (unsigned char *) &cal, eprom_memory.analog_cal_data.size);
    CAT25512_write_block(eprom_memory.twopoint_cal_data.start,
        (unsigned char *) &tp, eprom_memory.twopoint_cal_data.size);
    __enable_irq();
    adc_resume();

    // Apply immediately (no reboot needed).
    PrecomputeCalibration();
    SetTwoPointInputCalibration(adc2_min, adc2_max);
    SetSliderCalibration(v_min, v_max, t_min, t_max);

    DISPLAY_LED_I_OFF;
    DISPLAY_LED_II_OFF;
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

  // Precompute scalers from calibration data (legacy gain-only baseline).
  PrecomputeCalibration();

  // Optional two-point calibration: upgrade inputs to offset+gain and apply
  // per-slider calibration. Absent/corrupt -> keep the gain-only baseline and
  // raw (passthrough) sliders, i.e. the older behaviour.
  StoredTwoPointCal tp;
  CAT25512_read_block(
      eprom_memory.twopoint_cal_data.start,
      (unsigned char *) &tp,
      eprom_memory.twopoint_cal_data.size);
  if (marf_stored_twopoint_valid(&tp)) {
    SetTwoPointInputCalibration(tp.payload.adc2_min, tp.payload.adc2_max);
    SetSliderCalibration(tp.payload.v_min, tp.payload.v_max,
                         tp.payload.t_min, tp.payload.t_max);
  } else {
    ClearSliderCalibration();
  }
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

          // Restore each AFG's saved stage shift (section). The factory presets
          // are two-part and save AFG 2 on stages 17-32, so loading one and
          // starting both generators plays the whole arrangement with no manual
          // shift. (Sections only apply on a 16-slider board; with the expander
          // all 32 stages share one section, so leave the shift alone.)
          if (!Is_Expander_Present()) {
            AfgSetSection(AFG1, saved_program.payload.section[0] & 1);
            AfgSetSection(AFG2, saved_program.payload.section[1] & 1);
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

        // Capture per-sequence scale/root and each AFG's stage shift (section)
        for (uint8_t a = 0; a < 2; a++) {
          saved_program.payload.scale[a] = afg_scale[a];
          saved_program.payload.root[a]  = afg_root[a];
        }
        saved_program.payload.section[0] = AfgGetControllerState(AFG1).section;
        saved_program.payload.section[1] = AfgGetControllerState(AFG2).section;

        // Stamp magic/version/CRC, then write the record to the EPROM
        marf_stored_program_finalize(&saved_program);
        CAT25512_write_block(
            eprom_memory.programs[program_num].start,
            (unsigned char *) &saved_program,
            eprom_memory.programs[program_num].size);

        // This slot is now the user's: protect it from factory bank updates.
        FactoryMarkUserSave(program_num);

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

    // Start injected conversion. Bounded wait so a stuck conversion can never
    // hang the whole module (this loop does not pet the watchdog).
    ADC_SoftwareStartInjectedConv(ADC2);
    uint32_t guard = 100000;
    while (ADC_GetFlagStatus(ADC2, ADC_FLAG_JEOC) == RESET && --guard) {}
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
    AfgProcessModeChanges(AFG1, controller_job_flags.afg1_interrupts,
                          controller_job_flags.afg1_pulse_stamp);
    controller_job_flags.afg1_interrupts = PULSE_INPUTS_NONE;
    controller_job_flags.afg1_pulse_stamp = 0;
  }
  if (any_pulses_high(controller_job_flags.afg2_interrupts)) {
    AfgProcessModeChanges(AFG2, controller_job_flags.afg2_interrupts,
                          controller_job_flags.afg2_pulse_stamp);
    controller_job_flags.afg2_interrupts = PULSE_INPUTS_NONE;
    controller_job_flags.afg2_pulse_stamp = 0;
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

