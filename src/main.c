#include <stm32f4xx.h>
#include "stm32f4xx_syscfg.h"
#include <stm32f4xx_rcc.h>
#include <stm32f4xx_gpio.h>
#include <stm32f4xx_exti.h>
#include <stm32f4xx_tim.h>
#include <string.h> // memset only

#include "MAX5135.h"
#include "HC165.h"
#include "adc_pots_selector.h"
#include "CAT25512.h"
#include "leds_step.h"
#include "leds_modes.h"
#include "data_types.h"
#include "dip_config.h"
#include "expander.h"
#include "program.h"
#include "delays.h"
#include "analog_data.h"
#include "afg.h"
#include "display.h"
#include "controller.h"
#include "cycle_counter.h"
#include "eprom.h"
#include "presets.h"
#include "constants.h"
#include "watchdog.h"
#include "turing.h"
#include "marf_version.h"

// Dip switch state
volatile uDipConfig dip_config;

// Clocks
RCC_ClocksTypeDef RCC_Clocks;

// ADC interrupt handler
void ADC_IRQHandler() {
  volatile uint8_t stage = 0;

  uint8_t adc1_eoc = ADC_GetITStatus(ADC1, ADC_IT_EOC) == SET;
  uint8_t adc2_eoc = ADC_GetITStatus(ADC2, ADC_IT_EOC) == SET;

  if (adc1_eoc) ADC_ClearITPendingBit(ADC1, ADC_IT_EOC);
  if (adc2_eoc) ADC_ClearITPendingBit(ADC2, ADC_IT_EOC);

  if (controller_job_flags.inhibit_adc) {
    return;
  }

#if MARF_HW == 1
  // First conversion after a mux advance may have been sampled on the
  // previous channel (the conversion pipeline overlaps the long v1 chain
  // write): drop it. The next conversion is clean and drives the scan on.
  if (controller_job_flags.adc_discard_next) {
    controller_job_flags.adc_discard_next = 0;
    return;
  }
#endif

  if (controller_job_flags.adc_pot_sel < 16 && adc1_eoc) {
    // POT_TYPE_VOLTAGE EOC
    stage = controller_job_flags.adc_pot_sel;
    WriteVoltageSlider(stage, ADC1->DR);
    controller_job_flags.adc_mux_shift_out = 1;
  }
  else if (controller_job_flags.adc_pot_sel < 24 && adc2_eoc) {
    // POT_TYPE_OTHER EOC
    stage = controller_job_flags.adc_pot_sel - 16;
    WriteOtherCv(stage, ADC2->DR);
    controller_job_flags.adc_mux_shift_out = 1;
  }
  else if (controller_job_flags.adc_pot_sel < 40 && adc1_eoc) {
    // POT_TYPE_TIME EOC
    stage = controller_job_flags.adc_pot_sel - 24;
    WriteTimeSlider(stage, ADC1->DR);
    controller_job_flags.adc_mux_shift_out = 1;
  }
  else if (controller_job_flags.adc_pot_sel < 56 && adc1_eoc) {
    // More POT_TYPE_VOLTAGE EOC
    stage = controller_job_flags.adc_pot_sel - 24;
    WriteVoltageSlider(stage, ADC1->DR);
    controller_job_flags.adc_mux_shift_out = 1;
  } else if (adc1_eoc) {
    // More POT_TYPE_TIME EOC
    stage = controller_job_flags.adc_pot_sel - 40;
    WriteTimeSlider(stage, ADC1->DR);
    controller_job_flags.adc_mux_shift_out = 1;
  }
}


// Init ADCs with timer 2 as source for ADC start conversion
void mADC_init(void)
{
  GPIO_InitTypeDef GPIO_Init_user;
  ADC_InitTypeDef ADC_InitType;
  TIM_TimeBaseInitTypeDef TimeBaseInit;
  NVIC_InitTypeDef nvicStructure;

  //Timer init
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

  TIM_TimeBaseStructInit(&TimeBaseInit);
  TimeBaseInit.TIM_Prescaler 			= 1000;
  TimeBaseInit.TIM_CounterMode 		= TIM_CounterMode_Up;
  TimeBaseInit.TIM_Period 				= 6;
  TimeBaseInit.TIM_ClockDivision 	= TIM_CKD_DIV1;
  TIM_TimeBaseInit(TIM2, &TimeBaseInit); 

  TIM_SelectOutputTrigger(TIM2, TIM_TRGOSource_OC2Ref);
  TIM_CCxCmd(TIM2, TIM_Channel_2, TIM_CCx_Enable);
  TIM_SetCompare2(TIM2, 1);
  TIM2->CCMR1 |= TIM_CCMR1_OC2M;
  TIM_Cmd(TIM2, ENABLE); 

  // ADC Init
  NVIC_SetPriority (ADC_IRQn, 0);

  // ADC GPIO Init
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  memset(&GPIO_Init_user, 0, sizeof(GPIO_Init_user));
  GPIO_Init_user.GPIO_Pin 	= GPIO_Pin_0|GPIO_Pin_1;
  GPIO_Init_user.GPIO_Mode 	= GPIO_Mode_AN; //Analog mode
  GPIO_Init(GPIOA, & GPIO_Init_user);

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC2, ENABLE);

  ADC_CommonInitTypeDef ADC_CommonInitStructure;
  ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
  ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div6;
  ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
  ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
  ADC_CommonInit(&ADC_CommonInitStructure);

  ADC_StructInit(&ADC_InitType);
  ADC_InitType.ADC_ContinuousConvMode 	= DISABLE;
  ADC_InitType.ADC_DataAlign 						= ADC_DataAlign_Right;
  ADC_InitType.ADC_ExternalTrigConv 		= ADC_ExternalTrigConv_T2_TRGO;
  ADC_InitType.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
  ADC_InitType.ADC_NbrOfConversion 			= 1;
  ADC_InitType.ADC_Resolution 					= ADC_Resolution_12b;
  ADC_InitType.ADC_ScanConvMode 				= DISABLE;

  ADC_Init(ADC1, &ADC_InitType);
  ADC_Init(ADC2, &ADC_InitType);
  ADC_RegularChannelConfig(ADC1, ADC_Channel_0 ,1, ADC_SampleTime_480Cycles);
  ADC_RegularChannelConfig(ADC2, ADC_Channel_1 ,1, ADC_SampleTime_480Cycles);

  ADC_InjectedSequencerLengthConfig(ADC2, 1);
  ADC_InjectedChannelConfig(ADC2, ADC_Channel_1, 1 , ADC_SampleTime_480Cycles);

  // ADC interrupts init
  nvicStructure.NVIC_IRQChannel = ADC_IRQn;
  nvicStructure.NVIC_IRQChannelPreemptionPriority = 0;
  nvicStructure.NVIC_IRQChannelSubPriority = 0;
  nvicStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvicStructure);

  NVIC_EnableIRQ(ADC_IRQn);
  ADC_ITConfig(ADC1, ADC_IT_EOC, ENABLE);
  ADC_ITConfig(ADC2, ADC_IT_EOC, ENABLE);
  ADC_Cmd(ADC1, ENABLE);
  ADC_Cmd(ADC2, ENABLE);
};

// External interrupts init for start, stop and strobe
void mInterruptInit(void) {
  GPIO_InitTypeDef mGPIO;
  EXTI_InitTypeDef mInt;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

  mGPIO.GPIO_Mode = GPIO_Mode_IN;
  mGPIO.GPIO_Pin = MARF_PULSE_GPIO_PINS;
  mGPIO.GPIO_PuPd = GPIO_PuPd_NOPULL;
  mGPIO.GPIO_Speed = GPIO_Speed_100MHz;
  GPIO_Init(GPIOB, &mGPIO);

  MARF_PULSE_SYSCFG_INIT();   // configure the EXTI sources wired on this board

  // START-STOP LINE INIT Interrupt
  EXTI_DeInit();
  mInt.EXTI_Line = MARF_PULSE_EXTI_LINES;
  mInt.EXTI_Mode = EXTI_Mode_Interrupt;
  mInt.EXTI_Trigger = MARF_PULSE_EXTI_TRIGGER;   // v1 = rising+falling, v2 = rising
  mInt.EXTI_LineCmd = ENABLE;
  EXTI_Init(&mInt);

  NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority 	= 0x0F; // lower
  NVIC_InitStructure.NVIC_IRQChannelSubPriority 				= 0x00; 
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
  NVIC_Init(&NVIC_InitStructure);

  NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority 	= 0x0F; // lower
  NVIC_InitStructure.NVIC_IRQChannelSubPriority 				= 0x00; 
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
  NVIC_Init(&NVIC_InitStructure);

  NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority 	= 0x00; // highest
  NVIC_InitStructure.NVIC_IRQChannelSubPriority 				= 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

#if MARF_PULSE_HAS_EXTI2_15
  // v1 strobes sit on PB2 (EXTI2) and PB14 (EXTI15_10), which need their own IRQs.
  NVIC_InitStructure.NVIC_IRQChannel = EXTI2_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
#endif

  // Clear any latched pending bits for the lines this board uses.
  EXTI_ClearITPendingBit(MARF_PULSE_EXTI_LINES);
};

// SWD-visible debug taps for the pulse path (each stage of the funnel).
volatile uint32_t dbg_pulse_isr = 0;     // handler invocations
volatile uint32_t dbg_pulse_flag1 = 0;   // invocations with any AFG1 EXTI flag
volatile uint32_t dbg_pulse_lvl1 = 0;    // ... that also passed the level mask
volatile uint32_t dbg_pulse_q1 = 0;      // ... that were queued/processed

// General handler for any and all of the start, stop and strobe interrupt signals.
// The logic is more robust if all of the signals are coalesced and processed together.
void HandlePulseInterruptSignals() {
  static uint32_t pulse1_handled_time = 0;
  static uint32_t pulse2_handled_time = 0;
  uint32_t now = get_millis();
  dbg_pulse_isr++;

  // Stamp the pulse arrival with the DWT cycle counter NOW (never 0), so the
  // clock-follow period measurement isn't skewed by the modal ADC scan that
  // runs before the pulses are actually processed.
  uint32_t stamp = DWT->CYCCNT | 1;

  // Read all 3 GPIO pins directly right now.
  // flags* are the raw EXTI pending flags (always cleared below); pulses* are
  // what we act on.
  volatile PulseInputs flags1 = get_afg1_pulse_interrupts();
  volatile PulseInputs flags2 = get_afg2_pulse_interrupts();
  volatile PulseInputs pulses1 = flags1;
  volatile PulseInputs pulses2 = flags2;

#if MARF_HW == 1
  // v1 EXTI fires on BOTH edges (the input conditioning inverts, so the
  // LEADING edge of a jack pulse is the line's FALLING edge). Only act on the
  // edge where the pulse is ACTIVE (accessors return active-state), so each
  // pulse is processed exactly once, on its leading edge - the trailing edge
  // would otherwise read as a second event, and its slow pull-up rise split
  // Start/Stop into separate events (breaking the start+stop advance).
  // The raw flags are still cleared for both edges below.
  {
    PulseInputs lv1 = get_afg1_pulse_inputs();
    PulseInputs lv2 = get_afg2_pulse_inputs();
    pulses1.start &= lv1.start; pulses1.stop &= lv1.stop; pulses1.strobe &= lv1.strobe;
    pulses2.start &= lv2.start; pulses2.stop &= lv2.stop; pulses2.strobe &= lv2.strobe;
  }
#endif

  if (any_pulses_high(flags1)) dbg_pulse_flag1++;
  if (any_pulses_high(pulses1)) dbg_pulse_lvl1++;

  // Inhibit pulse handling for 2ms after anything changes
  if (now - pulse1_handled_time > 2) {
    // Debouncing ... don't trigger if the switches might have bounced back to low values
    if (any_pulses_high(pulses1)) {
      dbg_pulse_q1++;
      if (controller_job_flags.modal_loop == CONTROLLER_MODAL_NONE) {
        // We may need newly updated values from adc2 to do the right thing.
        // Go into a short modal state and then process the input pulses.
        controller_job_flags.afg1_interrupts = pulses1;
        controller_job_flags.afg1_pulse_stamp = stamp;
        controller_job_flags.modal_loop = CONTROLLER_MODAL_SCAN;
      } else {
        // Can't go into the modal scan because we're already in the save/load modal
        // So process immediately instead
        AfgProcessModeChanges(AFG1, pulses1, stamp);
      }
      pulse1_handled_time = now;
    }
  } else if (now < pulse1_handled_time) {
    // In case of millis overflow
    pulse1_handled_time = 0;
  }

  if (now - pulse2_handled_time > 2) {
    // Debouncing
    if (any_pulses_high(pulses2)) {
      if (controller_job_flags.modal_loop == CONTROLLER_MODAL_NONE) {
        controller_job_flags.afg2_interrupts = pulses2;
        controller_job_flags.afg2_pulse_stamp = stamp;
        controller_job_flags.modal_loop = CONTROLLER_MODAL_SCAN;
      } else {
        AfgProcessModeChanges(AFG2, pulses2, stamp);
      }
      pulse2_handled_time = now;
    }
  } else if (now < pulse2_handled_time) {
    pulse2_handled_time = 0;
  }

  // Clear all interrupt flags that were raised (both edges on v1, so a
  // masked-out falling edge can never leave a pending flag re-firing the IRQ)

  if (flags1.start)  EXTI_ClearITPendingBit(EXTI_LINE_START1);
  if (flags1.stop)   EXTI_ClearITPendingBit(EXTI_LINE_STOP1);
  if (flags1.strobe) EXTI_ClearITPendingBit(EXTI_LINE_STROBE1);

  if (flags2.start)  EXTI_ClearITPendingBit(EXTI_LINE_START2);
  if (flags2.stop)   EXTI_ClearITPendingBit(EXTI_LINE_STOP2);
  if (flags2.strobe) EXTI_ClearITPendingBit(EXTI_LINE_STROBE2);
};

// AFG1 stop interrupt
void EXTI0_IRQHandler() {
  // AFG1 stop signal rising edge
  delay_us(2);
  HandlePulseInterruptSignals();
};

// AFG2 stop interrupt
void EXTI1_IRQHandler() {
  // AFG2 stop signal rising edge
  delay_us(2);
  HandlePulseInterruptSignals();
};

// Interrupt handler for start and strobe signals both sections.
void EXTI9_5_IRQHandler() {
  delay_us(2);
  HandlePulseInterruptSignals();
}

#if MARF_PULSE_HAS_EXTI2_15
// v1 only: strobe A on PB2 (EXTI2) and strobe B on PB14 (EXTI15_10).
void EXTI2_IRQHandler() {
  delay_us(2);
  HandlePulseInterruptSignals();
}

void EXTI15_10_IRQHandler() {
  delay_us(2);
  HandlePulseInterruptSignals();
}
#endif

/*
	Timer interrupt handler for AFG1 clock.
	Every interrupt of Timer 4 triggers new output voltages and a check if the step has ended.
 */
void TIM4_IRQHandler() {
  ProgrammedOutputs afg1_outputs;
  static SlopingOutput sloping_output;
  static uint8_t tick_counter = 0;
  uint32_t ticks_left = 0;

  // Clear interrupt flag for Timer 4
  TIM4->SR = (uint16_t) ~TIM_IT_Update;
  __disable_irq();

  // We will recalculate the function every TICKS_WINDOW (32) ticks.
  // If the step has less ticks than that left, then process only until the end of the step.
  if (tick_counter == 0) {
    ticks_left = AfgGetStepTicksRemaining(AFG1);
    tick_counter = (ticks_left < TICKS_WINDOW) ? ticks_left : TICKS_WINDOW;

    // Process one time window and return the programmed output levels
    afg1_outputs = AfgTick(AFG1, get_afg1_pulse_inputs(), tick_counter);

    // Configure the sloping output to interpolate over the window
    if (afg1_outputs.sloped) {
      sloping_output.level = sloping_output.target_level;
      sloping_output.target_level = afg1_outputs.voltage;
      sloping_output.increment =
          (sloping_output.target_level - sloping_output.level) /
          (float) tick_counter;
    } else {
      sloping_output.level = afg1_outputs.voltage;
      sloping_output.target_level = afg1_outputs.voltage;
      sloping_output.increment = 0.0;
    }

    // Update output pulses
    if (afg1_outputs.all_pulses) {
      PULSE_LED_I_ALL_ON;
    } else {
      PULSE_LED_I_ALL_OFF;
    }
    // On units with reversed pulse wiring (selected in calibration), swap which
    // output drives jack 1 vs jack 2 so it matches the switch/LED.
    uint8_t a1_p1 = swapped_pulse_switches ? afg1_outputs.pulse2 : afg1_outputs.pulse1;
    uint8_t a1_p2 = swapped_pulse_switches ? afg1_outputs.pulse1 : afg1_outputs.pulse2;
    if (a1_p1) {
      PULSE_LED_I_1_ON;
    } else {
      PULSE_LED_I_1_OFF;
    }
    if (a1_p2) {
      PULSE_LED_I_2_ON;
    } else {
      PULSE_LED_I_2_OFF;
    }

    // Update the external dacs (slow)
    MAX5135_DAC_send(MAX5135_DAC_CH_0, afg1_outputs.time);
    MAX5135_DAC_send(MAX5135_DAC_CH_1, afg1_outputs.ref);
  }

  // Interpolate output
  sloping_output.level += sloping_output.increment;

  // Update internal dac (fast)
  DAC_SetChannel1Data(DAC_Align_12b_R,
      (uint16_t) sloping_output.level + 0.5);

  tick_counter -= 1;
  __enable_irq();
};

/*
  Timer interrupt handler for AFG2 clock.
  Keep changes in sync with TIM4_IRQHandler().
 */
void TIM5_IRQHandler() {
  ProgrammedOutputs afg2_outputs;
  static SlopingOutput sloping_output;
  static uint8_t tick_counter = 0;
  uint32_t ticks_left = 0;

  // Clear interrupt flag for Timer 5
  TIM5->SR = (uint16_t) ~TIM_IT_Update;
  __disable_irq();

  // We will recalculate the function every TICKS_WINDOW (32) ticks.
  // If the step has less ticks than that left, then process only until the end of the step.
  if (tick_counter == 0) {
     ticks_left = AfgGetStepTicksRemaining(AFG2);
     tick_counter = (ticks_left < TICKS_WINDOW) ? ticks_left : TICKS_WINDOW;

     // Process one time window and return the programmed output levels
     afg2_outputs = AfgTick(AFG2, get_afg2_pulse_inputs(), tick_counter);

     // Configure the sloping output to interpolate over the window
     if (afg2_outputs.sloped) {
       sloping_output.level = sloping_output.target_level;
       sloping_output.target_level = afg2_outputs.voltage;
       sloping_output.increment =
           (sloping_output.target_level - sloping_output.level) /
           (float) tick_counter;
     } else {
       sloping_output.level = afg2_outputs.voltage;
       sloping_output.target_level = afg2_outputs.voltage;
       sloping_output.increment = 0.0;
     }

     // Update output pulses
     if (afg2_outputs.all_pulses) {
       PULSE_LED_II_ALL_ON;
     } else {
       PULSE_LED_II_ALL_OFF;
     }
     uint8_t a2_p1 = swapped_pulse_switches ? afg2_outputs.pulse2 : afg2_outputs.pulse1;
     uint8_t a2_p2 = swapped_pulse_switches ? afg2_outputs.pulse1 : afg2_outputs.pulse2;
     if (a2_p1) {
       PULSE_LED_II_1_ON;
     } else {
       PULSE_LED_II_1_OFF;
     }
     if (a2_p2) {
       PULSE_LED_II_2_ON;
     } else {
       PULSE_LED_II_2_OFF;
     }

     // Update the external dacs (slow)
     MAX5135_DAC_send(MAX5135_DAC_CH_2, afg2_outputs.time);
     MAX5135_DAC_send(MAX5135_DAC_CH_3, afg2_outputs.ref);
  }

  // Interpolate output
  sloping_output.level += sloping_output.increment;

  // Update internal dac (fast)
  DAC_SetChannel2Data(DAC_Align_12b_R,
      (uint16_t) sloping_output.level + 0.5);

  tick_counter -= 1;
  __enable_irq();
};


#define AIRCR_VECTKEY_MASK    ((uint32_t)0x05FA0000)

// Init Timers 4 and 5 to control afg function generation
void mTimersInit(void) {
  TIM_TimeBaseInitTypeDef myTimer;
  NVIC_InitTypeDef nvicStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

  // Set up timers 4 and 5 to run at 32kHz.
  // Use a large prescaler so we can do long durations of up to 2 minutes.

  TIM_TimeBaseStructInit(&myTimer);
  myTimer.TIM_Prescaler = AFG_TIMER_PRESCALER;
  myTimer.TIM_Period = 1;
  myTimer.TIM_ClockDivision = TIM_CKD_DIV1;
  myTimer.TIM_CounterMode = TIM_CounterMode_Up;

  TIM_TimeBaseInit(TIM4, &myTimer);
  TIM_ARRPreloadConfig(TIM4, ENABLE);
  TIM_Cmd(TIM4, ENABLE);

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);
  TIM_TimeBaseInit(TIM5, &myTimer);
  TIM_ARRPreloadConfig(TIM5, ENABLE);
  TIM_Cmd(TIM5, ENABLE);

  TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
  TIM_ITConfig(TIM5, TIM_IT_Update, ENABLE);

  nvicStructure.NVIC_IRQChannel = TIM4_IRQn;
  nvicStructure.NVIC_IRQChannelPreemptionPriority = 1;
  nvicStructure.NVIC_IRQChannelSubPriority = 1;
  nvicStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvicStructure);

  nvicStructure.NVIC_IRQChannel = TIM5_IRQn;
  nvicStructure.NVIC_IRQChannelPreemptionPriority = 1;
  nvicStructure.NVIC_IRQChannelSubPriority = 1;
  nvicStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvicStructure);

  NVIC_SetPriority (TIM4_IRQn, 1);
  NVIC_SetPriority (TIM5_IRQn, 1);

  SCB->AIRCR = AIRCR_VECTKEY_MASK | NVIC_PriorityGroup_0;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM14, ENABLE);

  TIM_TimeBaseStructInit(&myTimer);
  myTimer.TIM_Prescaler = 210;
  myTimer.TIM_Period = 640;
  myTimer.TIM_ClockDivision = TIM_CKD_DIV1;
  myTimer.TIM_CounterMode = TIM_CounterMode_Up;

  TIM_TimeBaseInit(TIM8, &myTimer);

  TIM_Cmd(TIM8, DISABLE);
  TIM_ITConfig(TIM8, TIM_IT_Update, ENABLE);
  NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);

};

// Timer 8 IRQ. No longer used
void TIM8_UP_TIM13_IRQHandler(void) {
  if(TIM_GetITStatus(TIM8, TIM_IT_Update) != RESET) {
    TIM_Cmd(TIM8, DISABLE);
    TIM_ClearITPendingBit(TIM8, TIM_IT_Update);
  }
}

// Timer 14 IRQ now drives the mode-LED PWM (TIM8_TRG_COM_TIM14_IRQHandler is
// defined in display.c).

// Timer Interrupt handler for start scan section 1
// This is only used when timer is started by sustain or enable mode
void TIM3_IRQHandler() {
  TIM3->SR = (uint16_t) ~TIM_IT_Update;
  // Read through the accessor: it handles the per-hardware pin AND v1's
  // inverted input conditioning (the old hardcoded PB8 read silently broke
  // Sustain/Enable on v1 twice over).
  uint8_t start_signal = get_afg1_pulse_inputs().start;
  if (AfgCheckStart(AFG1, start_signal)) {
    TIM3->CR1 &= ~TIM_CR1_CEN;
  }
};

// Timer Interrupt handler for start scan section 1
// This is only used when timer is started by sustain or enable mode
void TIM7_IRQHandler() {
  TIM7->SR = (uint16_t) ~TIM_IT_Update;
  uint8_t start_signal = get_afg2_pulse_inputs().start;
  if (AfgCheckStart(AFG2, start_signal)) {
    TIM7->CR1 &= ~TIM_CR1_CEN;
  }
};

// Clear switch scan
void TIM6_DAC_IRQHandler() {
  ControllerCheckClear();
};

// Init GPIO for pulse outputs
void PulsesGpioInit() {
  GPIO_InitTypeDef GPIO_Pulses;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
  memset(&GPIO_Pulses, 0, sizeof(GPIO_Pulses));
  GPIO_Pulses.GPIO_Pin 		= PULSE_LED_I_ALL|PULSE_LED_I_1|PULSE_LED_I_2;
  GPIO_Pulses.GPIO_Mode 	= GPIO_Mode_OUT;
  GPIO_Pulses.GPIO_OType	= GPIO_OType_PP;
  GPIO_Pulses.GPIO_PuPd		= GPIO_PuPd_NOPULL;
  GPIO_Pulses.GPIO_Speed	= GPIO_Speed_100MHz;

  GPIO_Init(GPIOB, &GPIO_Pulses);

  PULSE_LED_I_ALL_OFF;
  PULSE_LED_I_1_OFF;
  PULSE_LED_I_2_OFF;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  memset(&GPIO_Pulses, 0, sizeof(GPIO_Pulses));
  GPIO_Pulses.GPIO_Pin 		= PULSE_LED_II_ALL|PULSE_LED_II_1|PULSE_LED_II_2;
  GPIO_Pulses.GPIO_Mode 	= GPIO_Mode_OUT;
  GPIO_Pulses.GPIO_OType	= GPIO_OType_PP;
  GPIO_Pulses.GPIO_PuPd		= GPIO_PuPd_NOPULL;
  GPIO_Pulses.GPIO_Speed	= GPIO_Speed_100MHz;

  GPIO_Init(GPIOA, &GPIO_Pulses);

  PULSE_LED_II_ALL_OFF;
  PULSE_LED_II_1_OFF;
  PULSE_LED_II_2_OFF;
};

// Init GPIOs for display leds
void DisplayLedsIOInit(void) {
  GPIO_InitTypeDef GPIO_InitStructure;
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  GPIO_InitStructure.GPIO_Pin 	= DISPLAY_LED_I|DISPLAY_LED_II;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
  GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
};

// Init internal DAC
void InternalDACInit(void) {
  DAC_InitTypeDef mDacInit;
  GPIO_InitTypeDef mGPIO_InitStructure;

  DAC_StructInit(&mDacInit);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

  // GPIOs init

  mGPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_4|GPIO_Pin_5;
  mGPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
  GPIO_Init(GPIOA, &mGPIO_InitStructure);

  /* DAC Periph clock enable */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

  //DAC init
  mDacInit.DAC_Trigger 				= DAC_Trigger_None;
  mDacInit.DAC_OutputBuffer 	= DAC_OutputBuffer_Disable;
  mDacInit.DAC_WaveGeneration = DAC_WaveGeneration_None;

  DAC_DeInit();

  DAC_Init(DAC_Channel_1, &mDacInit);
  DAC_Init(DAC_Channel_2, &mDacInit);

  DAC_Cmd(DAC_Channel_1, ENABLE);
  DAC_Cmd(DAC_Channel_2, ENABLE);

  DAC_SetChannel1Data(DAC_Align_12b_R, 0);
  DAC_SetChannel2Data(DAC_Align_12b_R, 0);
};


int main(void) {
  uButtons switches;

  // Initialize all the peripherals

  start_cycle_timer();
  InitProgram();
  RCC_GetClocksFreq(&RCC_Clocks);
  systickInit(1000);
  PulsesGpioInit();
  DisplayLedsIOInit();
  DipConfig_init();
  dip_config = GetDipConfig();
  SetVoltageRange(dip_config);
  Init_Expander();
  CAT25512_init();
  EpromInitializeMemoryLayout();
  LEDS_modes_init();
  LED_STEP_init();
  HC165_InitializeGPIO();
  MAX5135_Initialize();
  AdcMuxGpioInitialize();
  AdcMuxResetAllOff();
  mADC_init();
  AdcMuxResetAllOff();
  mTimersInit();
  mInterruptInit();
  InternalDACInit();
  DisplayAllInitialize();
  ModeLedPwmInit();
  turing_machines_init();

  // Settle down
  delay_ms(50);

  // Scan initial state
  switches.value = HC165_ReadSwitches();

  if (!switches.b.StageAddress1Advance) {
    // If advance 1 switch is pressed, enter calibration loop
    ControllerCalibrationLoop();
  } else {
    ControllerLoadCalibration();
  }

  // Seed the factory preset bank into any never-saved / erased program slots.
  // User-saved slots are preserved. Runs after calibration (which may erase the
  // chip) so a fresh module always boots with playable presets.
  PopulateFactoryPresets();

  // Start the watchdog only now that init (and any calibration, which does a
  // multi-second full-chip erase) is done. The refresh lives in the run loop.
  WatchdogInit();

  ControllerMainLoop(); // does not return
};

