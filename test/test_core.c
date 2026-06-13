/*
 * Host unit tests for the MARF core sequencer/DSP math.
 * Builds program.c + analog_data.c against the host shim (see test/shim).
 *
 *   make test
 */
#include <stdio.h>
#include <math.h>

#include "program.h"
#include "analog_data.h"
#include "dip_config.h"
#include "scales.h"

/* ---- tiny test framework ------------------------------------------------- */
int g_run = 0, g_fail = 0;   // shared with other test_*.c units
void run_storage_tests(void);
void run_scales_tests(void);
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_NEAR(a, b, eps) do { \
    g_run++; \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) > (eps)) { g_fail++; \
      printf("  FAIL %s:%d  %s ~= %s  (%.3f vs %.3f)\n", __FILE__, __LINE__, #a, #b, _a, _b); } \
  } while (0)

/* ---- helpers ------------------------------------------------------------- */
static void use_1v2_per_octave(void) {
  uDipConfig dip = {0};
  dip.b.V_OUT_1V2 = 1;       /* 1.2 V/octave */
  SetVoltageRange(dip);
}

static void clear_steps(void) {
  has_expander = 0;
  InitProgram();             /* all steps: FullRange + 30s range, sliders 0 */
}

/* ========================================================================= */
static void test_step_voltage_full_range(void) {
  printf("test_step_voltage_full_range\n");
  clear_steps();
  sliders[0].VLevel = 2048;          /* internal slider, full range, no quantize */
  CHECK_NEAR(GetStepVoltage(0, 0), 2048.0, 0.5);

  sliders[1].VLevel = 9000;          /* above 12-bit range -> clamps to 4095 */
  CHECK_NEAR(GetStepVoltage(0, 1), 4095.0, 0.5);
}

static void test_step_voltage_quantize(void) {
  printf("test_step_voltage_quantize\n");
  clear_steps();
  use_1v2_per_octave();
  steps[0].b.Quantize = 1;
  sliders[0].VLevel = 2048;
  float v = GetStepVoltage(0, 0);
  /* Output must be an integer number of semitones and within half a semitone. */
  extern float semitone_offset;
  double semis = v / semitone_offset;
  CHECK_NEAR(semis, round(semis), 0.001);     /* lands exactly on a semitone */
  CHECK(fabs(v - 2048.0) <= semitone_offset);  /* nearest semitone to input */
}

static void test_step_voltage_section_shift(void) {
  printf("test_step_voltage_section_shift\n");
  clear_steps();
  /* section 1 reads steps[16+n] programming but slider[n] (physical slider) */
  sliders[0].VLevel = 1000;
  steps[16].b.FullRange = 1;
  CHECK_NEAR(GetStepVoltage(1, 0), 1000.0, 0.5);
}

static void test_step_width(void) {
  printf("test_step_width\n");
  clear_steps();

  /* Full slider on the 30s range, x1 multiplier -> 30 s * 32 kHz = 960000 */
  sliders[0].TLevel = 4095;
  CHECK_NEAR((double) GetStepWidth(0, 0, 1.0f), 960000.0, 1.0);

  /* Minimum slider -> floor of 2 s = 64000 ticks */
  sliders[1].TLevel = 0;
  CHECK_NEAR((double) GetStepWidth(0, 1, 1.0f), 64000.0, 1.0);

  /* The 3 s range scales by 0.1 */
  sliders[2].TLevel = 4095;
  steps[2].b.TimeRange_30 = 0;
  steps[2].b.TimeRange_3 = 1;
  CHECK_NEAR((double) GetStepWidth(0, 2, 1.0f), 96000.0, 1.0);

  /* Time multiplier scales linearly */
  CHECK_NEAR((double) GetStepWidth(0, 0, 2.0f), 1920000.0, 2.0);
}

static void test_get_next_step_wrap(void) {
  printf("test_get_next_step_wrap\n");
  clear_steps();                       /* no expander -> max step 15 */
  CHECK(GetNextStep(0, 5) == 6);
  CHECK(GetNextStep(0, 15) == 0);      /* wrap around */
}

static void test_get_next_step_loop(void) {
  printf("test_get_next_step_loop\n");
  clear_steps();
  steps[2].b.CycleFirst = 1;
  steps[5].b.CycleLast = 1;
  /* end of loop at 5 searches back to the nearest first (2) */
  CHECK(GetNextStep(0, 5) == 2);
  /* a normal step still just advances */
  CHECK(GetNextStep(0, 3) == 4);
}

static void test_step_voltage_scale_quantize(void) {
  printf("test_step_voltage_scale_quantize\n");
  clear_steps();
  use_1v2_per_octave();
  extern float semitone_offset;
  current_scale = SCALE_MAJOR;
  steps[0].b.Quantize = 1;
  /* Every quantized output must land on a C-major scale degree. */
  for (int vl = 0; vl <= 4095; vl += 137) {
    sliders[0].VLevel = (uint16_t) vl;
    float v = GetStepVoltage(0, 0);
    int semi = (int) (v / semitone_offset + 0.5f);
    int pc = ((semi % 12) + 12) % 12;
    CHECK((scale_mask(SCALE_MAJOR) >> pc) & 1u);
  }
  current_scale = SCALE_CHROMATIC;   /* restore default */
}

static void test_get_next_step_loop_to_zero(void) {
  printf("test_get_next_step_loop_to_zero\n");
  clear_steps();
  steps[7].b.CycleLast = 1;            /* last with no first -> falls back to 0 */
  CHECK(GetNextStep(0, 7) == 0);
}

int main(void) {
  test_step_voltage_full_range();
  test_step_voltage_quantize();
  test_step_voltage_section_shift();
  test_step_width();
  test_get_next_step_wrap();
  test_get_next_step_loop();
  test_get_next_step_loop_to_zero();
  test_step_voltage_scale_quantize();
  run_storage_tests();
  run_scales_tests();

  printf("\n%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
