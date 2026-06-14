#ifndef __DISPLAY_H
#define __DISPLAY_H

#include <stm32f4xx.h>

#include "program.h"
#include "leds_modes.h"

// Union with flags which allows to update different parts of panel
typedef union {
  struct {
    unsigned char MainDisplay:1;
    unsigned char StepsDisplay:1;
    unsigned char OT1:1;
    unsigned char OT2:1;
    unsigned char OT3:1;
    unsigned char OT4:1;
    unsigned char OT5:1;
    unsigned char OT6:1;
  } b;
  unsigned char value;
} uDisplayUpdateFlag;

// Flags which can be set by anything to update the led displays
extern volatile uDisplayUpdateFlag display_update_flags;

// Bit mask of lit step LEDs (active low). Exposed so the controller can
// override the step display (e.g. to show a scale number while selecting).
extern uint32_t steps_leds_lit;

// Mode/programming LEDs (active low). Exposed so the controller can override
// an indicator (e.g. blink the voltage-source LED in Turing mode).
extern uLeds mode_leds_lit;

// When set, the voltage-source mode LED "breathes" (PWM via TIM14) to indicate
// Turing mode. Set by the controller from the displayed sequence's state.
extern volatile uint8_t mode_led_breathe;

// Start the mode-LED PWM timer (TIM14). Call once at boot.
void ModeLedPwmInit(void);

// Display modes
#define DISPLAY_MODE_VIEW_1       0
#define DISPLAY_MODE_VIEW_2       1
#define DISPLAY_MODE_EDIT_1       2
#define DISPLAY_MODE_EDIT_2       3

#define DISPLAY_MODE_SAVE         4
#define DISPLAY_MODE_LOAD         5

// Current display mode
extern volatile uint8_t display_mode;

// Do the pulse LEDs need to be swapped?
extern uint8_t swapped_pulses;

static inline void update_display() {
  display_update_flags.b.MainDisplay  = 1;
  display_update_flags.b.StepsDisplay = 1;
}

static inline void update_main_display() {
  display_update_flags.b.MainDisplay  = 1;
}

static inline void update_steps_display() {
  display_update_flags.b.StepsDisplay = 1;
}

void DisplayAllInitialize();

// Update display leds in programming section
void UpdateModeSectionLeds(AfgControllerState afg1, AfgControllerState afg2, uint8_t edit_mode_step_section, uint8_t edit_mode_step_num);

// Update step leds for normal op state
void UpdateStepSectionLeds(AfgControllerState afg1, AfgControllerState afg2, uint8_t edit_mode_step_num);

// Flush updates out to led shift registers
void FlushLedUpdates();

// Flash leds for clear
void RunClearAnimation();

// Triple-flash all step leds to signal a refused / empty action
void RunErrorAnimation();

// Flash leds for calibration mode
void RunCalibrationAnimation();

// Flash leds for save and load program
void RunSaveProgramAnimation();
void RunLoadProgramAnimation();

void RunWaitingLoadSaveAnimation(AfgControllerState afg1, AfgControllerState afg2);

// Light a single step
void StepLedsLightSingleStep(uint8_t step);

#endif
