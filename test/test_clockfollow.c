/*
 * Host unit tests for the external clock-follow helpers (ratio zones with
 * hysteresis, per-step nudge, period tracking, step widths).
 */
#include <stdio.h>

#include "clockfollow.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_clockfollow_tests(void);

static void test_ratio_zones(void) {
  printf("test_ratio_zones\n");
  // Extremes, and x1 anchored to the PANEL's printed "1" - each hardware
  // revision has its own MEASURED anchors (different pot tapers).
#if MARF_HW == 1
  enum { X1_MARK = 2013, X1_LO = 1763, X1_HI = 2263, DIV_W = 252, MUL_W = 262 };
#else
  enum { X1_MARK = 1517, X1_LO = 1267, X1_HI = 1767, DIV_W = 181, MUL_W = 333 };
#endif
  CHECK(cf_ratio_from_knob(0, 0) == -8);        // full CCW = /8
  CHECK(cf_ratio_from_knob(4095, 0) == 8);      // full CW  = x8
  CHECK(cf_ratio_from_knob(X1_MARK, 0) == 1);   // printed "1" = x1
  CHECK(cf_ratio_from_knob(X1_LO, 0) == 1);
  CHECK(cf_ratio_from_knob(X1_HI, 0) == 1);
  CHECK(cf_ratio_from_knob(X1_LO - 1, 0) == -2);
  CHECK(cf_ratio_from_knob(X1_HI + 1, 0) == 2);
  // Every divide zone in order
  for (int i = 0; i < 7; i++) {
    int8_t r = cf_ratio_from_knob((uint16_t) (i * DIV_W + DIV_W / 2), 0);
    CHECK(r == (int8_t) (i - 8));
  }
  // Every multiply zone in order
  for (int i = 0; i < 7; i++) {
    int8_t r = cf_ratio_from_knob((uint16_t) (X1_HI + 1 + i * MUL_W + MUL_W / 2), 0);
    CHECK(r == (int8_t) (i + 2));
  }
}

static void test_ratio_hysteresis(void) {
  printf("test_ratio_hysteresis\n");
#if MARF_HW == 1
  enum { HX1_MARK = 2013, HX1_LO = 1763, HX1_HI = 2263 };
#else
  enum { HX1_MARK = 1517, HX1_LO = 1267, HX1_HI = 1767 };
#endif
  // Sitting at x1; drift just past the zone edge stays x1...
  CHECK(cf_ratio_from_knob(HX1_HI + 1 + 20, 1) == 1);
  CHECK(cf_ratio_from_knob(HX1_LO - 20, 1) == 1);
  // ...but a clear move switches
  CHECK(cf_ratio_from_knob(HX1_HI + 1 + 60, 1) == 2);
  CHECK(cf_ratio_from_knob(HX1_LO - 60, 1) == -2);
  // And from x2, jitter back across the x1 edge doesn't flap
  CHECK(cf_ratio_from_knob(HX1_HI - 20, 2) == 2);
  CHECK(cf_ratio_from_knob(HX1_HI - 60, 2) == 1);
  // Invalid current ratio is ignored (no hysteresis applied)
  CHECK(cf_ratio_from_knob(HX1_MARK, 0) == 1);
  CHECK(cf_ratio_from_knob(HX1_MARK, 99) == 1);
}

static void test_humanize(void) {
  printf("test_humanize\n");
  const uint32_t W = 32000;   // 1 s base step
  // Slider down (incl. dead zone) = zero depth = dead on the grid
  CHECK(cf_humanize_depth(0, W) == 0);
  CHECK(cf_humanize_depth(150, W) == 0);
  // Depth grows with the slider on a squared curve, capped at W/4
  uint32_t d_low = cf_humanize_depth(1200, W);
  uint32_t d_mid = cf_humanize_depth(2600, W);
  uint32_t d_hi  = cf_humanize_depth(3900, W);
  uint32_t d_max = cf_humanize_depth(4095, W);
  CHECK(d_low > 0);
  CHECK(d_low < d_mid && d_mid < d_hi && d_hi <= d_max);
  CHECK(d_max == W / 2);
  // Linear: half slider gives about half the max depth
  uint32_t d_half = cf_humanize_depth(2148, W);   // dead zone + half travel
  CHECK(d_half > d_max * 2 / 5 && d_half < d_max * 3 / 5);
  // Units: deterministic in rnd, bounded, both signs occur
  int pos = 0, neg = 0;
  for (uint32_t r = 1; r < 400; r++) {
    uint32_t rnd = r * 2654435761u;
    int32_t u = cf_humanize_unit(rnd);
    CHECK(u == cf_humanize_unit(rnd));    // pure in rnd
    CHECK(u >= -4095 && u <= 4095);
    if (u > 0) pos++;
    if (u < 0) neg++;
  }
  CHECK(pos > 50 && neg > 50);            // spread both early and late
  // Offset scaling: full unit = +/- depth; zero depth kills any unit
  CHECK(cf_humanize_offset(4095, d_max) == (int32_t) d_max);
  CHECK(cf_humanize_offset(-4095, d_max) == -(int32_t) d_max);
  CHECK(cf_humanize_offset(2048, d_max) > 0);
  CHECK(cf_humanize_offset(2048, d_max) < (int32_t) d_max);
  CHECK(cf_humanize_offset(4095, 0) == 0);
}

static void test_period_tracking(void) {
  printf("test_period_tracking\n");
  // First measurement is taken as-is
  CHECK(cf_update_period(0, 16000) == 16000);
  // Small jitter smooths toward the new value
  uint32_t p = cf_update_period(16000, 16400);
  CHECK(p > 16000 && p < 16400);
  // Tempo change snaps
  CHECK(cf_update_period(16000, 32000) == 32000);
  CHECK(cf_update_period(32000, 8000) == 8000);
}

static void test_step_widths(void) {
  printf("test_step_widths\n");
  const uint32_t P = 16000;   // 0.5 s clock
  // Base widths for divide / unity / multiply
  CHECK(cf_base_width(P, 1) == P);
  CHECK(cf_base_width(P, -4) == P * 4);
  CHECK(cf_base_width(P, 8) == P / 8);
  // Sub-steps can never collapse below the floor
  CHECK(cf_base_width(100, 8) == CF_MIN_WIDTH_TICKS);
  // Neutral offsets = base width
  CHECK(cf_step_width(P, 1, 0, 0) == P);
  // Next step late -> this step longer; next early -> shorter
  CHECK(cf_step_width(P, 1, 0, (int32_t) (P / 4)) == P + P / 4);
  CHECK(cf_step_width(P, 1, 0, -(int32_t) (P / 8)) == P - P / 8);
  // Current step started late -> effectively shorter
  CHECK(cf_step_width(P, 1, (int32_t) (P / 4), 0) == P - P / 4);
  // Worst case (cur full late, next full early) still >= base/5
  CHECK(cf_step_width(P, 1, (int32_t) (P / 4), -(int32_t) (P / 4)) >= P / 5);
}

void run_clockfollow_tests(void) {
  test_ratio_zones();
  test_ratio_hysteresis();
  test_humanize();
  test_period_tracking();
  test_step_widths();
}
