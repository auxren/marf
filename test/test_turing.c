/*
 * Host unit tests for the Turing-machine shift-register engine.
 * These compile without MARF_HW defined, so they exercise the v2 (default)
 * monotonic "slip" mapping: slider bottom = locked loop, slider top = every
 * step slips, squared curve in between.
 */
#include <stdio.h>

#include "turing.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_turing_tests(void);

#define BOTTOM 0      // slider down -> locked loop
#define TOP    4095   // slider up   -> every step slips
#define MID    2048   // middle      -> occasional slip

static void test_length_clamp(void) {
  printf("test_length_clamp\n");
  TuringMachine t;
  turing_seed(1);
  turing_init(&t, 1);   CHECK(t.length == TURING_MIN_LENGTH);
  turing_init(&t, 99);  CHECK(t.length == TURING_MAX_LENGTH);
  turing_init(&t, 8);   CHECK(t.length == 8);
}

static void test_value_range(void) {
  printf("test_value_range\n");
  TuringMachine t;
  turing_seed(42);
  turing_init(&t, 8);
  for (int i = 0; i < 200; i++) {
    turing_clock(&t, MID);
    uint16_t v = turing_value(&t);
    CHECK(v <= 4095);
  }
}

static void test_bottom_locks(void) {
  printf("test_bottom_locks  (slider down -> the same `length` voltages repeat)\n");
  TuringMachine t;
  turing_seed(7);
  turing_init(&t, 4);
  // Prime: the 8-bit read window needs 8 clocks to flush the (non-periodic)
  // seed bits before the locked sequence repeats exactly.
  for (int i = 0; i < 8; i++) turing_clock(&t, BOTTOM);
  uint16_t a[4];
  for (int i = 0; i < 4; i++) { turing_clock(&t, BOTTOM); a[i] = turing_value(&t); }
  // The 4-value sequence repeats exactly, pass after pass.
  for (int rep = 0; rep < 10; rep++) {
    for (int i = 0; i < 4; i++) {
      turing_clock(&t, BOTTOM);
      CHECK(turing_value(&t) == a[i]);
    }
  }
  // And the values are full-range reads (8-bit window), not a sliver confined
  // by the short loop length.
  uint16_t max = 0;
  for (int i = 0; i < 4; i++) if (a[i] > max) max = a[i];
  CHECK(max > 512);
  int distinct = 0;
  for (int i = 0; i < 4; i++) {
    int seen = 0;
    for (int j = 0; j < i; j++) if (a[j] == a[i]) { seen = 1; break; }
    if (!seen) distinct++;
  }
  CHECK(distinct >= 2);   // a sequence, not a drone
}

static void test_dead_zone_locks(void) {
  printf("test_dead_zone_locks  (bottom dead-zone -> still locked)\n");
  TuringMachine t;
  turing_seed(21);
  turing_init(&t, 6);
  for (int i = 0; i < 8; i++) turing_clock(&t, 150);   // flush the read window
  uint16_t a[6];
  for (int i = 0; i < 6; i++) { turing_clock(&t, 150); a[i] = turing_value(&t); }
  for (int rep = 0; rep < 5; rep++) {
    for (int i = 0; i < 6; i++) {
      turing_clock(&t, 150);
      CHECK(turing_value(&t) == a[i]);
    }
  }
}

static void test_top_all_slips(void) {
  printf("test_top_all_slips  (slider up -> the sequence never repeats)\n");
  TuringMachine t;
  turing_seed(12345);
  turing_init(&t, 4);
  // Collect a long run; it must not settle into the register's period (4).
  uint16_t seq[64];
  for (int i = 0; i < 64; i++) { turing_clock(&t, TOP); seq[i] = turing_value(&t); }
  int distinct = 0;
  for (int i = 0; i < 64; i++) {
    int seen = 0;
    for (int j = 0; j < i; j++) if (seq[j] == seq[i]) { seen = 1; break; }
    if (!seen) distinct++;
  }
  CHECK(distinct > 3);                 // visibly varying, not a 4-state loop
  int period4 = 1;
  for (int i = 4; i < 64; i++) if (seq[i] != seq[i - 4]) { period4 = 0; break; }
  CHECK(!period4);                     // definitely not locked to the length
}

// Count how many of `clocks` clocks slipped (fed back a bit that differs from
// the loop tap). A slip that happens to write the same bit is invisible; that
// only makes this an undercount, which is fine for monotonicity checks.
static int count_slips(uint16_t change, int clocks, uint32_t seed) {
  TuringMachine t;
  turing_seed(seed);
  turing_init(&t, 8);
  int slips = 0;
  for (int i = 0; i < clocks; i++) {
    uint8_t expect = (uint8_t) ((t.bits >> 7) & 1u);   // loop tap (length 8)
    turing_clock(&t, change);
    if ((uint8_t) (t.bits & 1u) != expect) slips++;
  }
  return slips;
}

static void test_slip_curve_monotonic(void) {
  printf("test_slip_curve_monotonic  (more slider -> more slips, squared)\n");
  int lo  = count_slips(1000, 4000, 99);
  int mid = count_slips(2048, 4000, 99);
  int hi  = count_slips(3500, 4000, 99);
  CHECK(lo < mid);
  CHECK(mid < hi);
  // Squared curve: the low quarter of the (post-dead-zone) travel should slip
  // rarely - clearly less than half the mid rate.
  CHECK(lo * 2 < mid);
}

void run_turing_tests(void) {
  test_length_clamp();
  test_value_range();
  test_bottom_locks();
  test_dead_zone_locks();
  test_top_all_slips();
  test_slip_curve_monotonic();
}
