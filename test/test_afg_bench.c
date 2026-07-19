/*
 * Virtual bench: host tests for the REAL AFG tick engine (afg.c).
 *
 * Drives AfgTick() in the same 32-tick (1 ms) windows the TIM4 ISR uses on
 * hardware, injects external clock pulses through AfgProcessModeChanges()
 * with simulated DWT stamps (exactly what the pulse EXTI path does), and
 * emulates the TIM3 sustain/enable poll by calling AfgCheckStart() with the
 * gate level while an AFG is holding.
 *
 * These scenarios reproduce, in simulation, the bug classes found on real
 * hardware with a logic analyzer (v3.2.1/v3.2.2 bring-up):
 *   - the swallowed-onset bug (PulseWidth 0 triggers never fired on clocked
 *     advances because step_cnt's pre-increment consumed the window),
 *   - clock-follow lock / integer ratios / gate widths,
 *   - sustain & enable holds under a gate.
 *
 * The whole suite builds and runs for BOTH hardware variants (MARF_HW=2 and
 * MARF_HW=1): the engine must behave identically on both, so a change
 * validated by these tests on a v2 bench carries to REV1.
 */
#include <stdio.h>
#include <string.h>

#include "afg.h"
#include "analog_data.h"
#include "program.h"
#include "turing.h"
#include "clockfollow.h"
#include "constants.h"

extern int g_run, g_fail;
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_afg_bench_tests(void);

/* ---- simulated time ------------------------------------------------------ */

extern volatile uint32_t millis;          /* test_support.c; get_millis() reads it */
extern volatile uint32_t dbg_cfadv[8];    /* afg.c SWD taps: [3]=div skip [6]=jump */

#define WINDOW_TICKS 32u                            /* 1 ms at 32 kHz */
#define CYCLES_PER_TICK (168000000u / AFG_TICK_FREQUENCY)

static uint32_t sim_ticks;                /* total ticks elapsed */
static PulseInputs no_pulses;             /* all-zero level inputs */

/* Run one 1 ms tick window with the given LEVEL inputs; returns the outputs
 * held for that window. Mirrors the TIM4 ISR cadence. */
static ProgrammedOutputs win(PulseInputs level) {
  ProgrammedOutputs o = AfgTick(AFG1, level, WINDOW_TICKS);
  sim_ticks += WINDOW_TICKS;
  millis = sim_ticks / 32u;
  AfgRecalculateStepWidths();   /* the hardware main loop does this too */
  return o;
}

/* Inject an external clock pulse (a rising edge on Start+Stop together),
 * stamped the way the pulse EXTI handler stamps it. */
static void clock_pulse(void) {
  PulseInputs p = {};
  p.start = 1;
  p.stop = 1;
  AfgProcessModeChanges(AFG1, p, sim_ticks * CYCLES_PER_TICK + 1u);
}

static uint8_t cur_step(void) { return AfgGetControllerState(AFG1).step_num; }

/* ---- common setup --------------------------------------------------------- */

static void set_pw_all(uint8_t pw) {
  for (int i = 0; i < 32; i++) steps[i].b.PulseWidth = pw & 0xF;
}

static void bench_setup(uint16_t tm_knob) {
  InitProgram();
  turing_machines_init();
  turing_seed(12345);                       /* deterministic humanize draws */
  AfgAllInitialize();
  AfgHardStop(AFG1);
  AfgHardStop(AFG2);
  sim_ticks = 0;
  millis = 0;
  memset((void *) dbg_cfadv, 0, sizeof(uint32_t) * 8);

  /* Identity calibration so add_data values are used as-is. */
  for (int i = 0; i < 8; i++) { external_off[i] = 0.0f; external_scale[i] = 1.0f; }
  add_data[ADC_TIMEMULTIPLY_Ch_1] = tm_knob;
  add_data[ADC_TIMEMULTIPLY_Ch_2] = tm_knob;
  add_data[ADC_STAGEADDRESS_Ch_1] = 0;
  add_data[ADC_STAGEADDRESS_Ch_2] = 0;

  /* Time sliders down (short steps, no humanize while clocked). */
  for (int i = 0; i < 32; i++) { slider_raw_t[i] = 0; sliders[i].TLevel = 0; }

  AfgRecalculateStepWidths();
}

/* Establish an external clock lock at the given period (in 1 ms windows):
 * two pulses are enough (the second, with a plausible delta, locks). */
static void lock_clock(uint32_t period_windows) {
  clock_pulse();
  for (uint32_t i = 0; i < period_windows; i++) win(no_pulses);
  clock_pulse();
  AfgRecalculateStepWidths();               /* knob -> ratio */
}

/* ---- tests ----------------------------------------------------------------
 * Knob anchors are per-hardware (see clockfollow.c: each board's pot taper
 * was measured separately). Pick the measured panel-"1" and a mid-/2 and
 * mid-x2 zone value for the variant this binary is built for. */

#if MARF_HW == 1
  #define KNOB_X1   2013u   /* measured "1"; x1 zone 1763..2263 */
  #define KNOB_DIV2 1650u   /* centre of /2 zone (1512..1762)   */
  #define KNOB_X2   2400u   /* centre of x2 zone (2264..2525)   */
#else
  #define KNOB_X1   1517u   /* measured "1"; x1 zone 1267..1767 */
  #define KNOB_DIV2 1170u   /* centre of /2 zone (1086..1266)   */
  #define KNOB_X2   1930u   /* centre of x2 zone (1768..2100)   */
#endif

static void test_clocked_x1_lock_and_gate(void) {
  printf("bench: clocked x1 lock + 53%% gate\n");
  bench_setup(KNOB_X1);
  set_pw_all(8);                            /* 1 + 8*98/15 = 53% */

  const uint32_t P = 30;                    /* 30 ms clock period */
  lock_clock(P);

  /* Run 6 full periods; each pulse must advance exactly one step and the
   * gate must be high ~53% of the period. */
  uint32_t jumps_before = dbg_cfadv[6];
  for (int p = 0; p < 6; p++) {
    uint8_t step_before = cur_step();
    uint32_t high = 0;
    for (uint32_t i = 0; i < P; i++) {
      ProgrammedOutputs o = win(no_pulses);
      if (o.all_pulses) high++;
    }
    clock_pulse();
    CHECK(cur_step() == (uint8_t) ((step_before + 1) & 0x0F));
    /* 53% of 30 windows = 15.9 -> allow 14..18 */
    CHECK(high >= 14 && high <= 18);
  }
  CHECK(dbg_cfadv[6] - jumps_before >= 6);
}

/* Regression: the v3.2.2 swallowed-onset bug. A clocked advance happens in
 * the pulse ISR, BETWEEN tick windows; with PulseWidth 0 the fixed ~1 ms
 * trigger must still fire in the very next window. Before the pulse_onset
 * latch, step_cnt's pre-increment consumed it and the output stayed low. */
static void test_onset_trigger_after_isr_advance(void) {
  printf("bench: PW0 trigger fires on ISR-driven advances (onset latch)\n");
  bench_setup(KNOB_X1);
  set_pw_all(0);

  const uint32_t P = 30;
  lock_clock(P);

  for (int p = 0; p < 8; p++) {
    /* settle the rest of the period: trigger must be over after ~1 window */
    uint32_t high_late = 0;
    ProgrammedOutputs first = {0};
    for (uint32_t i = 0; i < P; i++) {
      ProgrammedOutputs o = win(no_pulses);
      if (i == 0) first = o;
      else if (o.all_pulses) high_late++;
    }
    clock_pulse();                          /* ISR advance between windows */
    if (p > 0) {
      CHECK(first.all_pulses == 1);         /* the 1 ms trigger fired */
      CHECK(high_late <= 1);                /* and it was ONLY a trigger */
    }
  }
}

static void test_divide_by_two(void) {
  printf("bench: /2 advances every second pulse\n");
  bench_setup(KNOB_DIV2);
  set_pw_all(8);

  const uint32_t P = 30;
  lock_clock(P);
  for (uint32_t i = 0; i < P; i++) win(no_pulses);

  uint8_t advances = 0, skips = 0;
  for (int p = 0; p < 8; p++) {
    uint8_t before = cur_step();
    uint32_t skip_before = dbg_cfadv[3];
    clock_pulse();
    if (cur_step() != before) advances++;
    if (dbg_cfadv[3] != skip_before) skips++;
    for (uint32_t i = 0; i < P; i++) win(no_pulses);
  }
  CHECK(advances == 4);                     /* every 2nd pulse */
  CHECK(skips == 4);                        /* the others counted as skips */
}

static void test_multiply_by_two(void) {
  printf("bench: x2 emits a sub-step between pulses\n");
  bench_setup(KNOB_X2);
  set_pw_all(0);                            /* 1 ms triggers = countable */

  const uint32_t P = 30;
  lock_clock(P);
  for (uint32_t i = 0; i < P; i++) win(no_pulses);
  clock_pulse();

  /* Over 4 periods, count trigger onsets: x2 = 2 per period (pulse-aligned
   * step + one internal sub-step). PW0 triggers are 1 window wide, so
   * rising edges are countable. */
  uint32_t onsets = 0;
  uint8_t prev_high = 0;
  for (int p = 0; p < 4; p++) {
    for (uint32_t i = 0; i < P; i++) {
      ProgrammedOutputs o = win(no_pulses);
      if (o.all_pulses && !prev_high) onsets++;
      prev_high = o.all_pulses;
    }
    clock_pulse();
  }
  CHECK(onsets >= 7 && onsets <= 9);        /* ~2/period; edges at the seams */
}

/* Sustain: at step end with the START gate HIGH the AFG holds (STAY_HI_Z)
 * and resumes when the gate drops. On hardware TIM3 polls the level and
 * calls AfgCheckStart; the bench does the same each window while holding. */
static void test_sustain_under_gate(void) {
  printf("bench: sustain holds while gate high, releases on fall\n");
  bench_setup(KNOB_X1);
  set_pw_all(8);
  steps[1].b.OpModeSUSTAIN = 1;             /* stage 2 sustains */
  steps[1].b.TimeRange_3 = 0;               /* short: 0.03s range */
  steps[1].b.TimeRange_p03 = 1;
  for (int i = 0; i < 32; i++) if (i != 1) {
    steps[i].b.TimeRange_3 = 0; steps[i].b.TimeRange_p03 = 1;
  }
  AfgRecalculateStepWidths();

  PulseInputs gate_high = {}; gate_high.start = 1;

  /* Start running (a lone start pulse from STOP). */
  PulseInputs start_pulse = {}; start_pulse.start = 1;
  AfgProcessModeChanges(AFG1, start_pulse, sim_ticks * CYCLES_PER_TICK + 1u);
  CHECK(AfgGetControllerState(AFG1).mode == MODE_RUN);

  /* Find the free-run cadence first (gate low): count windows per step. */
  uint32_t w = 0, guard = 0;
  uint8_t s0 = cur_step();
  while (cur_step() == s0 && guard++ < 4000) { win(no_pulses); w++; }
  CHECK(guard < 4000);
  CHECK(w >= 1 && w < 3500);                /* whatever the range gives */
  uint32_t hold_windows = (w * 8 > 200) ? w * 8 : 200;

  /* Run with the gate HIGH until stage 2 is playing, then to its end. */
  guard = 0;
  while (cur_step() != 1 && guard++ < 4000) { win(gate_high); }
  CHECK(guard < 4000);

  /* Stage 2 must HOLD far past its natural width while the gate stays high.
   * (AfgCheckStart is the TIM3 poll; with the gate high it must not resume.) */
  for (uint32_t i = 0; i < hold_windows; i++) {
    win(gate_high);
    AfgCheckStart(AFG1, 1);
  }
  CHECK(cur_step() == 1);

  /* Gate falls -> the TIM3 poll sees start low -> advance resumes. */
  uint32_t held = 0;
  while (cur_step() == 1 && held++ < 8) {
    win(no_pulses);
    AfgCheckStart(AFG1, 0);
  }
  CHECK(cur_step() == 2);
}

/* Enable: at step end with the START gate LOW the AFG waits (WAIT_HI_Z)
 * and resumes when the gate rises. */
static void test_enable_under_gate(void) {
  printf("bench: enable waits while gate low, releases on rise\n");
  bench_setup(KNOB_X1);
  set_pw_all(8);
  steps[1].b.OpModeENABLE = 1;
  for (int i = 0; i < 32; i++) {
    steps[i].b.TimeRange_3 = 0; steps[i].b.TimeRange_p03 = 1;
  }
  AfgRecalculateStepWidths();

  PulseInputs start_pulse = {}; start_pulse.start = 1;
  AfgProcessModeChanges(AFG1, start_pulse, sim_ticks * CYCLES_PER_TICK + 1u);

  /* Reach stage 2 with the gate low. */
  uint32_t guard = 0;
  while (cur_step() != 1 && guard++ < 4000) { win(no_pulses); }
  CHECK(guard < 4000);

  /* It must hold at stage 2 while the gate stays low. */
  for (uint32_t i = 0; i < 400; i++) {
    win(no_pulses);
    AfgCheckStart(AFG1, 0);
  }
  CHECK(cur_step() == 1);

  /* Gate rises -> resume. */
  uint32_t held = 0;
  while (cur_step() == 1 && held++ < 8) {
    PulseInputs gate_high = {}; gate_high.start = 1;
    win(gate_high);
    AfgCheckStart(AFG1, 1);
  }
  CHECK(cur_step() == 2);
}

/* Humanize: with a time slider up while clocked, latched offsets shift step
 * onsets but the engine must stay locked (advance exactly once per pulse,
 * counting pending/rushed paths). Deterministic via the seeded PRNG. */
static void test_humanize_stays_locked(void) {
  printf("bench: humanize offsets keep the 1:1 lock\n");
  bench_setup(KNOB_X1);
  set_pw_all(8);
  for (int i = 0; i < 32; i++) slider_raw_t[i] = 4095;   /* full humanize */

  const uint32_t P = 30;
  lock_clock(P);

  uint8_t start_step = cur_step();
  for (int p = 0; p < 16; p++) {
    for (uint32_t i = 0; i < P; i++) win(no_pulses);
    clock_pulse();
  }
  /* Let any pending (late) advance land. */
  for (uint32_t i = 0; i < P; i++) win(no_pulses);
  /* 17 boundary pulses total from lock_clock+loop; exactly one step each. */
  uint8_t expect = (uint8_t) ((start_step + 16) & 0x0F);
  CHECK(cur_step() == expect);
}

/* The Time/Ref outputs differ per hardware: v1 drives a 12-bit DAC, v2 a
 * 10-bit one. Pin the scaling for the variant this binary was built for. */
static void test_time_out_scaling(void) {
  printf("bench: time output scaling matches MARF_HW=%d\n", MARF_HW);
  bench_setup(KNOB_X1);
  sliders[cur_step()].TLevel = 4095;
  ProgrammedOutputs o = win(no_pulses);
#if MARF_HW == 1
  CHECK(o.time == 4095);                    /* full 12 bits */
#else
  CHECK(o.time == 4095 >> 2);               /* 10-bit DAC */
#endif
}

void run_afg_bench_tests(void) {
  test_clocked_x1_lock_and_gate();
  test_onset_trigger_after_isr_advance();
  test_divide_by_two();
  test_multiply_by_two();
  test_sustain_under_gate();
  test_enable_under_gate();
  test_humanize_stays_locked();
  test_time_out_scaling();
}
