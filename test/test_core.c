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
#include "turing.h"

/* ---- tiny test framework ------------------------------------------------- */
int g_run = 0, g_fail = 0;   // shared with other test_*.c units
void run_storage_tests(void);
void run_scales_tests(void);
void run_turing_tests(void);
void run_presets_tests(void);
void run_clockfollow_tests(void);
void run_afg_bench_tests(void);
void run_v1_invariants_tests(void);
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
  CHECK_NEAR(GetStepVoltage(0, 0, SCALE_CHROMATIC, 0, 0, 0), 2048.0, 0.5);

  sliders[1].VLevel = 9000;          /* above 12-bit range -> clamps to 4095 */
  CHECK_NEAR(GetStepVoltage(0, 1, SCALE_CHROMATIC, 0, 0, 0), 4095.0, 0.5);
}

static void test_step_voltage_quantize(void) {
  printf("test_step_voltage_quantize\n");
  clear_steps();
  use_1v2_per_octave();
  steps[0].b.Quantize = 1;
  sliders[0].VLevel = 2048;
  float v = GetStepVoltage(0, 0, SCALE_CHROMATIC, 0, 0, 0);
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
  CHECK_NEAR(GetStepVoltage(1, 0, SCALE_CHROMATIC, 0, 0, 0), 1000.0, 0.5);
}

static void test_step_width(void) {
  printf("test_step_width\n");
  clear_steps();

  /* Full slider on the 30s range, x1 multiplier -> 30 s * 32 kHz = 960000 */
  steps[0].b.TimeRange_3 = 0;
  steps[0].b.TimeRange_30 = 1;
  sliders[0].TLevel = 4095;
  CHECK_NEAR((double) GetStepWidth(0, 0, 1.0f), 960000.0, 1.0);

  /* Minimum slider -> floor of 2 s = 64000 ticks */
  steps[1].b.TimeRange_3 = 0;
  steps[1].b.TimeRange_30 = 1;
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
  steps[0].b.Quantize = 1;
  /* Every quantized output must land on a C-major scale degree. */
  for (int vl = 0; vl <= 4095; vl += 137) {
    sliders[0].VLevel = (uint16_t) vl;
    float v = GetStepVoltage(0, 0, SCALE_MAJOR, 0, 0, 0);
    int semi = (int) (v / semitone_offset + 0.5f);
    int pc = ((semi % 12) + 12) % 12;
    CHECK((scale_mask(SCALE_MAJOR) >> pc) & 1u);
  }
}

/* per-sequence scales: same step, two scales, each output stays in its scale */
static void test_per_sequence_scale(void) {
  printf("test_per_sequence_scale\n");
  clear_steps();
  use_1v2_per_octave();
  extern float semitone_offset;
  steps[0].b.Quantize = 1;
  sliders[0].VLevel = 2048;
  float a = GetStepVoltage(0, 0, SCALE_MAJOR, 0, 0, 0);          /* AFG I: C major */
  float b = GetStepVoltage(0, 0, SCALE_NATURAL_MINOR, 9, 0, 0);  /* AFG II: A minor */
  int sa = ((int)(a / semitone_offset + 0.5f) % 12 + 12) % 12;
  int sb = ((int)(b / semitone_offset + 0.5f) % 12 + 12) % 12;
  CHECK((scale_mask(SCALE_MAJOR) >> sa) & 1u);
  /* A natural minor with root 9: note must be in the root-shifted mask */
  CHECK((scale_mask(SCALE_NATURAL_MINOR) >> (((sb - 9) % 12 + 12) % 12)) & 1u);

  /* slider->scale/root mapping */
  CHECK(scale_from_slider(0) == SCALE_CHROMATIC);
  CHECK(scale_from_slider(4095) == SCALE_COUNT - 1);
  CHECK(root_from_slider(0) == 0);
  CHECK(root_from_slider(4095) == 11);
}

static void test_override_wins(void) {
  printf("test_override_wins\n");
  clear_steps();
  steps[0].b.VoltageSource = 1;     /* external source */
  steps[0].b.FullRange = 1;
  /* override (Turing / normalled external) is used regardless of source */
  CHECK_NEAR(GetStepVoltage(0, 0, SCALE_CHROMATIC, 0, 2000, 1), 2000.0, 0.5);
  /* without override, external source reads add_data (0 by default) */
  CHECK_NEAR(GetStepVoltage(0, 0, SCALE_CHROMATIC, 0, 0, 0), 0.0, 0.5);
}

static void test_get_next_step_loop_to_zero(void) {
  printf("test_get_next_step_loop_to_zero\n");
  clear_steps();
  steps[7].b.CycleLast = 1;            /* last with no first -> falls back to 0 */
  CHECK(GetNextStep(0, 7) == 0);
}

static void test_randomize_program(void) {
  printf("test_randomize_program\n");

  /* Randomize only block/section 1 (stages 17-32). Mark block 0 first so we can
   * prove it is left completely untouched. */
  for (uint8_t i = 0; i < 16; i++) {
    steps[i] = (uStep){{ 0xAA, 0xBB, 0xCC, 0xDD }};
    sliders[i].VLevel = 1111; sliders[i].TLevel = 2222;
  }
  unpin_all_sliders();

  turing_seed(0xC0FFEEu);
  RandomizeProgram(1);

  uint8_t firsts = 0, lasts = 0;
  for (uint8_t i = 16; i <= 31; i++) {
    int vr = steps[i].b.FullRange + steps[i].b.Voltage0 + steps[i].b.Voltage2
           + steps[i].b.Voltage4 + steps[i].b.Voltage6 + steps[i].b.Voltage8;
    CHECK(vr == 1);                 /* exactly one voltage range/octave */
    int tr = steps[i].b.TimeRange_p03 + steps[i].b.TimeRange_p3
           + steps[i].b.TimeRange_3 + steps[i].b.TimeRange_30;
    CHECK(tr == 1);                 /* exactly one time range */
    CHECK(steps[i].b.VoltageSource == 0);   /* sources kept internal */
    CHECK(steps[i].b.TimeSource == 0);
    CHECK(steps[i].b.OpModeSTOP == 0 && steps[i].b.OpModeSUSTAIN == 0
          && steps[i].b.OpModeENABLE == 0); /* op modes off */
    firsts += steps[i].b.CycleFirst;
    lasts  += steps[i].b.CycleLast;
  }
  CHECK(steps[16].b.CycleFirst == 1);       /* loop starts at block's stage 1 */
  CHECK(firsts == 1);
  CHECK(lasts == 1);                        /* exactly one (random) loop end */

  /* Block 0 (the other AFG) is untouched, and only block 1 is pinned. */
  for (uint8_t i = 0; i < 16; i++) {
    CHECK(sliders[i].VLevel == 1111 && sliders[i].TLevel == 2222);
    CHECK((voltage_slider_pins.high & (1UL << i)) == 0);
  }
  CHECK((voltage_slider_pins.high & 0xFFFF0000UL) == 0xFFFF0000UL);  /* block 1 pinned */
  CHECK((time_slider_pins.low     & 0xFFFF0000UL) == 0xFFFF0000UL);
}

static void test_twopoint_input_cal(void) {
  printf("test_twopoint_input_cal\n");
  uint16_t mn[8], mx[8];
  for (int i = 0; i < 8; i++) { mn[i] = 200; mx[i] = 4000; }
  SetTwoPointInputCalibration(mn, mx);
  add_data[0] = 200;  CHECK_NEAR(read_calibrated_add_data_float(0), 0.0, 1.0);
  add_data[0] = 4000; CHECK_NEAR(read_calibrated_add_data_float(0), 4095.0, 1.0);
  add_data[0] = 2100; CHECK_NEAR(read_calibrated_add_data_float(0),
                                 (2100.0 - 200.0) * 4095.0 / (4000.0 - 200.0), 1.0);
  add_data[0] = 50;   CHECK(read_calibrated_add_data_float(0) == 0.0);   /* clamp low */
  /* span too small -> falls back to gain-only (off 0); external_cal defaults to 1 */
  mn[1] = 2000; mx[1] = 2400;
  SetTwoPointInputCalibration(mn, mx);
  add_data[1] = 1500; CHECK_NEAR(read_calibrated_add_data_float(1), 1500.0 * external_cal[1], 1.0);
}

static void test_twopoint_slider_cal(void) {
  printf("test_twopoint_slider_cal\n");
  uint16_t vmn[32], vmx[32], tmn[32], tmx[32];
  for (int i = 0; i < 32; i++) { vmn[i] = 300; vmx[i] = 3900; tmn[i] = 300; tmx[i] = 3900; }
  SetSliderCalibration(vmn, vmx, tmn, tmx);
  unpin_all_sliders();
  scale_select_freeze = 0;
  for (int n = 0; n < 100; n++) WriteVoltageSlider(0, 3900);
  CHECK(sliders[0].VLevel >= 4080);                 /* max -> full scale */
  for (int n = 0; n < 100; n++) WriteVoltageSlider(0, 300);
  CHECK(sliders[0].VLevel <= 15);                   /* min -> zero */
  /* passthrough after clear */
  ClearSliderCalibration();
  for (int n = 0; n < 100; n++) WriteVoltageSlider(0, 2000);
  CHECK_NEAR((double) sliders[0].VLevel, 2000.0, 30.0);
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
  test_per_sequence_scale();
  test_override_wins();
  test_randomize_program();
  test_twopoint_input_cal();
  test_twopoint_slider_cal();
  run_storage_tests();
  run_scales_tests();
  run_turing_tests();
  run_presets_tests();
  run_clockfollow_tests();
  run_afg_bench_tests();
  run_v1_invariants_tests();

  printf("\nMARF_HW=%d: %d checks, %d failed\n", MARF_HW, g_run, g_fail);
  return g_fail ? 1 : 0;
}
