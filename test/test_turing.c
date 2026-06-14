/*
 * Host unit tests for the Turing-machine shift-register engine.
 */
#include <stdio.h>

#include "turing.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_turing_tests(void);

#define CW   4095   // far clockwise  -> locked loop
#define CCW  0      // far counter-cw -> double-locked loop
#define MID  2048   // centre         -> random

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

static void test_cw_locks(void) {
  printf("test_cw_locks  (full clockwise -> period == length)\n");
  TuringMachine t;
  turing_seed(7);
  turing_init(&t, 5);
  uint16_t a[5], b[5];
  for (int i = 0; i < 5; i++) { turing_clock(&t, CW); a[i] = turing_value(&t); }
  for (int i = 0; i < 5; i++) { turing_clock(&t, CW); b[i] = turing_value(&t); }
  for (int i = 0; i < 5; i++) CHECK(a[i] == b[i]);   // repeats every `length`
}

static void test_ccw_double_locks(void) {
  printf("test_ccw_double_locks  (full ccw -> period == 2*length)\n");
  TuringMachine t;
  turing_seed(7);
  turing_init(&t, 5);
  uint16_t a[10], b[10];
  for (int i = 0; i < 10; i++) { turing_clock(&t, CCW); a[i] = turing_value(&t); }
  for (int i = 0; i < 10; i++) { turing_clock(&t, CCW); b[i] = turing_value(&t); }
  for (int i = 0; i < 10; i++) CHECK(a[i] == b[i]);  // repeats every 2*length

  // and it is genuinely double-length: the two halves differ somewhere
  int halves_differ = 0;
  for (int i = 0; i < 5; i++) if (a[i] != a[i + 5]) halves_differ = 1;
  CHECK(halves_differ);
}

static void test_centre_randomises(void) {
  printf("test_centre_randomises  (centre -> not a short loop)\n");
  TuringMachine t;
  turing_seed(12345);
  turing_init(&t, 5);
  // Collect a long run; it must not repeat with the register's period (5).
  uint16_t seq[64];
  for (int i = 0; i < 64; i++) { turing_clock(&t, MID); seq[i] = turing_value(&t); }
  int distinct = 0;
  for (int i = 0; i < 64; i++) {
    int seen = 0;
    for (int j = 0; j < i; j++) if (seq[j] == seq[i]) { seen = 1; break; }
    if (!seen) distinct++;
  }
  CHECK(distinct > 3);                 // visibly varying, not a 5-state loop
  int period5 = 1;
  for (int i = 5; i < 64; i++) if (seq[i] != seq[i - 5]) { period5 = 0; break; }
  CHECK(!period5);                     // definitely not locked to the length
}

void run_turing_tests(void) {
  test_length_clamp();
  test_value_range();
  test_cw_locks();
  test_ccw_double_locks();
  test_centre_randomises();
}
