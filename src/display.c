#include "display.h"

#include <stm32f4xx.h>

#include "leds_modes.h"
#include "leds_step.h"
#include "program.h"
#include "afg.h"

// Flags which can be set by anything to update the led display
volatile uDisplayUpdateFlag display_update_flags;

// Current display mode state
volatile uint8_t display_mode = DISPLAY_MODE_VIEW_1;

// Do the pulse LEDs need to be swapped?
uint8_t swapped_pulses = 0;

uint32_t steps_leds_lit = 0xFFFFFFFF;

uLeds mode_leds_lit;

// ---- Mode-LED software PWM (for the Turing "breathing" indicator) ----------
// When mode_led_breathe is set, a fast TIM14 ISR drives the mode LED shift
// register and PWMs the voltage-source LED's duty cycle so it breathes (it
// stays lit, pulsing between ~25% and 100% brightness). FlushLedUpdates hands
// the ISR the latest mode-LED state; when not breathing the main loop sends the
// mode LEDs itself as usual.
volatile uint8_t mode_led_breathe = 0;
static volatile uLeds mode_led_snapshot;
static volatile uint8_t mode_led_snap_seq = 0;

void ModeLedPwmInit(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM14, ENABLE);
  TIM14->PSC = 83;            // 84 MHz / 84 = 1 MHz
  TIM14->ARR = 499;           // 1 MHz / 500 = 2 kHz tick (8-step PWM -> 250 Hz)
  TIM14->CNT = 0;
  TIM14->SR  = (uint16_t) ~TIM_IT_Update;
  TIM14->DIER = TIM_DIER_UIE;
  TIM14->CR1 |= TIM_CR1_CEN;
  // Below the audio (1) and ADC (0) IRQs so it can never delay them.
  NVIC_SetPriority(TIM8_TRG_COM_TIM14_IRQn, 3);
  NVIC_EnableIRQ(TIM8_TRG_COM_TIM14_IRQn);
}

void TIM8_TRG_COM_TIM14_IRQHandler(void) {
  static uint8_t phase = 0;
  static uint16_t frame = 0;
  static uint8_t brightness = 4;
  static uint8_t last_v = 0xFF;
  static uint8_t last_seq = 0xFF;

  if (!(TIM14->SR & TIM_IT_Update)) return;
  TIM14->SR = (uint16_t) ~TIM_IT_Update;

  if (!mode_led_breathe) { last_v = 0xFF; return; }

  phase = (phase + 1) & 7;                  // 8-step PWM frame
  if (phase == 0) {
    frame++;
    // Triangle breathe over ~1.5 s; duty 2..8 of 8 (never fully off).
    uint16_t t = frame % 376;
    uint16_t tri = (t < 188) ? t : (376 - t);
    brightness = (uint8_t) (2 + (uint32_t) tri * 6u / 188u);
  }

  uint8_t v = (phase < brightness) ? 0 : 1;  // active low: 0 = lit
  if (v != last_v || mode_led_snap_seq != last_seq) {
    uLeds out = mode_led_snapshot;
    out.b.VoltageSource = v;
    LEDS_modes_SendStruct(&out);
    last_v = v;
    last_seq = mode_led_snap_seq;
  }
}

void DisplayAllInitialize() {
  steps_leds_lit = 0xFFFFFFFF;
  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;
  mode_leds_lit.b.Seq1Stop = 0;
  mode_leds_lit.b.Seq2Stop = 0;

  display_update_flags.value = 0x00;
  display_update_flags.b.MainDisplay  = 1;
  display_update_flags.b.StepsDisplay = 1;

  display_mode = DISPLAY_MODE_VIEW_1;

  if (Is_Expander_Present()) {
    LED_STEP_SendWordExpanded(steps_leds_lit);
  } else {
    LED_STEP_SendWord(steps_leds_lit);
  }
  LEDS_modes_SendStruct(&mode_leds_lit);
}

// Update the led data for view and edit modes
void UpdateLedsProgramMode(uLeds* mLeds, uStep* step) {
  mLeds->b.VoltageFull   &= ~step->b.FullRange;
  mLeds->b.Voltage0      &= ~step->b.Voltage0;
  mLeds->b.Voltage2      &= ~step->b.Voltage2;
  mLeds->b.Voltage4      &= ~step->b.Voltage4;
  mLeds->b.Voltage6      &= ~step->b.Voltage6;
  mLeds->b.Voltage8      &= ~step->b.Voltage8;
  if (swapped_pulses) {
    mLeds->b.Pulse1        &= ~step->b.OutputPulse2;
    mLeds->b.Pulse2        &= ~step->b.OutputPulse1;
  } else {
    mLeds->b.Pulse1        &= ~step->b.OutputPulse1;
    mLeds->b.Pulse2        &= ~step->b.OutputPulse2;
  }
  mLeds->b.CycleFirst    &= ~step->b.CycleFirst;
  mLeds->b.CycleLast     &= ~step->b.CycleLast;
  mLeds->b.VoltageSource &= ~step->b.VoltageSource;
  mLeds->b.Integration   &= ~step->b.Sloped;
  mLeds->b.Quantization  &= ~step->b.Quantize;
  mLeds->b.TimeRange0    &= ~step->b.TimeRange_p03;
  mLeds->b.TimeRange1    &= ~step->b.TimeRange_p3;
  mLeds->b.TimeRange2    &= ~step->b.TimeRange_3;
  mLeds->b.TimeRange3    &= ~step->b.TimeRange_30;
  mLeds->b.TimeSource    &= ~step->b.TimeSource;
  mLeds->b.OPStop        &= ~step->b.OpModeSTOP;
  mLeds->b.OPSustain     &= ~step->b.OpModeSUSTAIN;
  mLeds->b.OPEnable      &= ~step->b.OpModeENABLE;
}

// Update mode and programming LEDs
void UpdateModeSectionLeds(AfgControllerState afg1, AfgControllerState afg2, uint8_t edit_mode_section, uint8_t edit_mode_step_num) {

  // AFG1 mode LEDs
  if (afg1.mode == MODE_RUN) {
    mode_leds_lit.b.Seq1Run &= 0;
  } else if (afg1.mode == MODE_WAIT  || afg1.mode == MODE_WAIT_HI_Z || afg1.mode == MODE_STAY_HI_Z) {
    mode_leds_lit.b.Seq1Wait &= 0;
  } else if (afg1.mode == MODE_STOP) {
    mode_leds_lit.b.Seq1Stop &= 0;
  };

  // AFG2 mode LEDs
  if (afg2.mode == MODE_RUN) {
    mode_leds_lit.b.Seq2Run &= 0;
  } else if (afg2.mode == MODE_WAIT || afg2.mode == MODE_WAIT_HI_Z  || afg2.mode == MODE_STAY_HI_Z) {
    mode_leds_lit.b.Seq2Wait &= 0;
  } else if (afg2.mode == MODE_STOP) {
    mode_leds_lit.b.Seq2Stop &= 0;
  };

  if ((display_mode == DISPLAY_MODE_VIEW_1) || (display_mode == DISPLAY_MODE_EDIT_1) ) {
    DISPLAY_LED_I_ON;
    DISPLAY_LED_II_OFF;
  } else if ((display_mode == DISPLAY_MODE_VIEW_2) || (display_mode == DISPLAY_MODE_EDIT_2) ) {
    DISPLAY_LED_II_ON;
    DISPLAY_LED_I_OFF;
  };

  uStep led_step;

  switch (display_mode) {
  case DISPLAY_MODE_VIEW_1:
    led_step = get_step_programming(afg1.section, afg1.step_num);
    UpdateLedsProgramMode(&mode_leds_lit, &led_step);
    break;
  case DISPLAY_MODE_EDIT_1:
    led_step = get_step_programming(edit_mode_section, edit_mode_step_num);
    UpdateLedsProgramMode(&mode_leds_lit, &led_step);
    break;
  case DISPLAY_MODE_VIEW_2:
    led_step = get_step_programming(afg2.section, afg2.step_num);
    UpdateLedsProgramMode(&mode_leds_lit, &led_step);
    break;
  case DISPLAY_MODE_EDIT_2:
    led_step = get_step_programming(edit_mode_section, edit_mode_step_num);
    UpdateLedsProgramMode(&mode_leds_lit, &led_step);
    break;
  }
}

// Mark the led lit/dirty and it will be lit in the next shift
void UpdateStepSectionLeds(AfgControllerState afg1, AfgControllerState afg2, uint8_t edit_mode_step_num) {
  if ( display_mode == DISPLAY_MODE_VIEW_1 ) {
    steps_leds_lit &= ~(1UL << afg1.step_num);
  };
  if ( display_mode == DISPLAY_MODE_VIEW_2 ) {
    steps_leds_lit &= ~(1UL << afg2.step_num);
  };
  if ( ( display_mode == DISPLAY_MODE_EDIT_1 ) ||
      ( display_mode == DISPLAY_MODE_EDIT_2 )) {
    steps_leds_lit &= ~(1UL << edit_mode_step_num);
  }
}

void RunClearAnimation() {
  if (Is_Expander_Present()) {
    for (uint8_t i = 0; i < 8; i++) {
      LED_STEP_SendWordExpanded(0x00000000);
      delay_ms(60);
      LED_STEP_SendWordExpanded(0xFFFFFFFF);
      delay_ms(60);
    }
  } else {
    for (uint8_t i = 0; i < 8; i++) {
      LED_STEP_SendWord(0x0000);
      delay_ms(60);
      LED_STEP_SendWord(0xFFFF);
      delay_ms(60);
    }
  }
}

// Quick triple flash of all step leds to signal a refused or empty action
// (e.g. loading a slot that holds no valid program).
void RunErrorAnimation() {
  for (uint8_t i = 0; i < 3; i++) {
    if (Is_Expander_Present()) {
      LED_STEP_SendWordExpanded(0x00000000);
      delay_ms(80);
      LED_STEP_SendWordExpanded(0xFFFFFFFF);
    } else {
      LED_STEP_SendWord(0x0000);
      delay_ms(80);
      LED_STEP_SendWord(0xFFFF);
    }
    delay_ms(80);
  }
}

// Chase the Quantize -> Sloped -> Full Range -> External mode LEDs a couple of
// times to acknowledge entering Turing mode.
void RunTuringEnterAnimation(void) {
  uLeds a;
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t i = 0; i < 4; i++) {
      a.value[0] = 0xFF; a.value[1] = 0xFF; a.value[2] = 0xFF; a.value[3] = 0xFF;
      if (i == 0)      a.b.Quantization = 0;   // active low: 0 = lit
      else if (i == 1) a.b.Integration = 0;    // Sloped
      else if (i == 2) a.b.VoltageFull = 0;    // Full Range
      else             a.b.VoltageSource = 0;  // External
      LEDS_modes_SendStruct(&a);
      delay_ms(90);
    }
  }
}

// Actually shift the lit leds out via the two shift registers
// and then reset everything.
void FlushLedUpdates() {
  if (Is_Expander_Present()) {
    LED_STEP_SendWordExpanded(steps_leds_lit);
  } else {
    LED_STEP_SendWord(steps_leds_lit);
  }
  steps_leds_lit = 0xFFFFFFFF;

  if (mode_led_breathe) {
    // The TIM14 ISR drives the mode LEDs (breathing the voltage-source LED);
    // just hand it the latest state.
    mode_led_snapshot = mode_leds_lit;
    mode_led_snap_seq++;
  } else {
    LEDS_modes_SendStruct(&mode_leds_lit);
  }
  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;
}

// Run/wait/stop leds cycling during calibration
// Called every 10ms
void RunCalibrationAnimation() {
  static uint16_t counter = 0x0000;

  if (counter < 20) {
    mode_leds_lit.b.Seq1Run = 1;
    mode_leds_lit.b.Seq1Wait = 1;
    mode_leds_lit.b.Seq1Stop = 0;
    mode_leds_lit.b.Seq2Run = 1;
    mode_leds_lit.b.Seq2Wait = 1;
    mode_leds_lit.b.Seq2Stop = 0;
  } else if (counter < 40) {
    mode_leds_lit.b.Seq1Run = 0;
    mode_leds_lit.b.Seq1Wait = 1;
    mode_leds_lit.b.Seq1Stop = 1;
    mode_leds_lit.b.Seq2Run = 0;
    mode_leds_lit.b.Seq2Wait = 1;
    mode_leds_lit.b.Seq2Stop = 1;
  } else if (counter < 60) {
    mode_leds_lit.b.Seq1Run = 1;
    mode_leds_lit.b.Seq1Wait = 0;
    mode_leds_lit.b.Seq1Stop = 1;
    mode_leds_lit.b.Seq2Run = 1;
    mode_leds_lit.b.Seq2Wait = 0;
    mode_leds_lit.b.Seq2Stop = 1;
  } else {
    counter = 0;
  }

  mode_leds_lit.b.Pulse1 = swapped_pulses;
  mode_leds_lit.b.Pulse2 = ~swapped_pulses;

  LEDS_modes_SendStruct(&mode_leds_lit);
  counter += 1;
}

// After saving a program, flash all mode leds in the programming section down
void RunSaveProgramAnimation() {
  // Start
  steps_leds_lit = 0xFFFF;
  LED_STEP_SendWord(steps_leds_lit);
  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;
  LEDS_modes_SendStruct(&mode_leds_lit);
  // Flash first row
  mode_leds_lit.b.Quantization = 0;
  mode_leds_lit.b.Integration = 0;
  mode_leds_lit.b.VoltageFull = 0;
  mode_leds_lit.b.VoltageSource = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Second row
  mode_leds_lit.b.Voltage0 = 0;
  mode_leds_lit.b.Voltage2 = 0;
  mode_leds_lit.b.Voltage4 = 0;
  mode_leds_lit.b.Voltage6 = 0;
  mode_leds_lit.b.Voltage8 = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Third row
  mode_leds_lit.b.OPStop = 0;
  mode_leds_lit.b.OPSustain = 0;
  mode_leds_lit.b.OPEnable = 0;
  mode_leds_lit.b.CycleFirst = 0;
  mode_leds_lit.b.CycleLast = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Fourth row
  mode_leds_lit.b.TimeRange0 = 0;
  mode_leds_lit.b.TimeRange1 = 0;
  mode_leds_lit.b.TimeRange0 = 0;
  mode_leds_lit.b.TimeRange1 = 0;
  mode_leds_lit.b.TimeSource = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Reset all off
  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;
  LEDS_modes_SendStruct(&mode_leds_lit);
}

// After saving a program, flash all mode leds in the programming section up
void RunLoadProgramAnimation() {
  // Start
  steps_leds_lit = 0xFFFF;
  LED_STEP_SendWord(steps_leds_lit);
  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;
  LEDS_modes_SendStruct(&mode_leds_lit);
  // Fourth row
  mode_leds_lit.b.TimeRange0 = 0;
  mode_leds_lit.b.TimeRange1 = 0;
  mode_leds_lit.b.TimeRange0 = 0;
  mode_leds_lit.b.TimeRange1 = 0;
  mode_leds_lit.b.TimeSource = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Third row
  mode_leds_lit.b.OPStop = 0;
  mode_leds_lit.b.OPSustain = 0;
  mode_leds_lit.b.OPEnable = 0;
  mode_leds_lit.b.CycleFirst = 0;
  mode_leds_lit.b.CycleLast = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Second row
  mode_leds_lit.b.Voltage0 = 0;
  mode_leds_lit.b.Voltage2 = 0;
  mode_leds_lit.b.Voltage4 = 0;
  mode_leds_lit.b.Voltage6 = 0;
  mode_leds_lit.b.Voltage8 = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Flash first row
  mode_leds_lit.b.Quantization = 0;
  mode_leds_lit.b.Integration = 0;
  mode_leds_lit.b.VoltageFull = 0;
  mode_leds_lit.b.VoltageSource = 0;
  LEDS_modes_SendStruct(&mode_leds_lit);
  delay_ms(50);
  // Reset all off
  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;
  LEDS_modes_SendStruct(&mode_leds_lit);
}

void StepLedsLightSingleStep(uint8_t step) {
  steps_leds_lit = 0xFFFF;
  steps_leds_lit &= ~(1UL << step);
  LED_STEP_SendWord(steps_leds_lit);
}

// Called approx every 1ms
// Just toggles pulse leds while waiting for step selection.

void RunWaitingLoadSaveAnimation(AfgControllerState afg1, AfgControllerState afg2) {
  static uint16_t counter = 0;

  mode_leds_lit.value[0] = 0xFF;
  mode_leds_lit.value[1] = 0xFF;
  mode_leds_lit.value[2] = 0xFF;
  mode_leds_lit.value[3] = 0xFF;

  if (counter < 300) {
    mode_leds_lit.b.Pulse1 = 1;
    mode_leds_lit.b.Pulse2 = 0;
    counter += 1;
  } else if (counter < 600) {
    mode_leds_lit.b.Pulse1 = 0;
    mode_leds_lit.b.Pulse2 = 1;
    counter += 1;
  } else {
    counter = 0;
  }

  // AFG1 mode LEDs
  if (afg1.mode == MODE_RUN) {
    mode_leds_lit.b.Seq1Run &= 0;
  } else if (afg1.mode == MODE_WAIT  || afg1.mode == MODE_WAIT_HI_Z || afg1.mode == MODE_STAY_HI_Z) {
    mode_leds_lit.b.Seq1Wait &= 0;
  } else if (afg1.mode == MODE_STOP) {
    mode_leds_lit.b.Seq1Stop &= 0;
  };

  // AFG2 mode LEDs
  if (afg2.mode == MODE_RUN) {
    mode_leds_lit.b.Seq2Run &= 0;
  } else if (afg2.mode == MODE_WAIT || afg2.mode == MODE_WAIT_HI_Z  || afg2.mode == MODE_STAY_HI_Z) {
    mode_leds_lit.b.Seq2Wait &= 0;
  } else if (afg2.mode == MODE_STOP) {
    mode_leds_lit.b.Seq2Stop &= 0;
  };

  LEDS_modes_SendStruct(&mode_leds_lit);
}
