/*
 * Hardware-variant invariants, pinned by test.
 *
 * Everything here encodes a fact MEASURED on real hardware during the REV1
 * bring-up (v3.2.1/v3.2.2). The suite builds twice (MARF_HW=2 and
 * MARF_HW=1); each binary asserts the truths for ITS variant, so a change
 * that silently breaks a variant-specific contract fails `make test`
 * without any hardware attached.
 */
#include <stdio.h>
#include <string.h>

#include "analog_data.h"
#include "marf_version.h"

extern int g_run, g_fail;
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_v1_invariants_tests(void);

/* The shim maps all GPIO ports onto one register file. */
extern GPIO_TypeDef _shim_gpio;

/* ---- pulse-input polarity -------------------------------------------------
 * v1 (rev 1.0 / v1.6): Start/Stop input conditioning INVERTS - the lines
 * idle HIGH and a pulse pulls them LOW (measured; it is also why v1 uses
 * both-edge EXTI with leading-edge level qualification). Strobes idle low
 * and read direct on both revisions.
 * v2: all pulse inputs read direct (high = active).
 * These accessors are the ONE place the polarity may live. */
static void test_pulse_input_polarity(void) {
  printf("invariants: pulse accessor polarity (MARF_HW=%d)\n", MARF_HW);

#if MARF_HW == 1
  /* Idle (lines pulled high): nothing active. */
  _shim_gpio.IDR = MARF_GPIO_START1 | MARF_GPIO_STOP1 |
                   MARF_GPIO_START2 | MARF_GPIO_STOP2;
  CHECK(get_afg1_pulse_inputs().start == 0);
  CHECK(get_afg1_pulse_inputs().stop == 0);
  CHECK(get_afg2_pulse_inputs().start == 0);
  CHECK(get_afg2_pulse_inputs().stop == 0);

  /* A pulse pulls START1 low: only AFG1 start goes active. */
  _shim_gpio.IDR &= ~(uint32_t) MARF_GPIO_START1;
  CHECK(get_afg1_pulse_inputs().start == 1);
  CHECK(get_afg1_pulse_inputs().stop == 0);
  CHECK(get_afg2_pulse_inputs().start == 0);

  /* Strobes are NOT inverted: idle low = inactive, high = active. */
  _shim_gpio.IDR = MARF_GPIO_START1 | MARF_GPIO_STOP1 |
                   MARF_GPIO_START2 | MARF_GPIO_STOP2;
  CHECK(get_afg1_pulse_inputs().strobe == 0);
  _shim_gpio.IDR |= MARF_GPIO_STROBE1;
  CHECK(get_afg1_pulse_inputs().strobe == 1);
  CHECK(get_afg2_pulse_inputs().strobe == 0);
  _shim_gpio.IDR |= MARF_GPIO_STROBE2;
  CHECK(get_afg2_pulse_inputs().strobe == 1);

  /* Pin assignments (from the v1.6 source, bench-verified): START1=PB7,
   * STOP1=PB0, STROBE1=PB2, START2=PB5, STOP2=PB1, STROBE2=PB14. */
  CHECK(MARF_GPIO_START1 == GPIO_Pin_7);
  CHECK(MARF_GPIO_STOP1 == GPIO_Pin_0);
  CHECK(MARF_GPIO_STROBE1 == GPIO_Pin_2);
  CHECK(MARF_GPIO_START2 == GPIO_Pin_5);
  CHECK(MARF_GPIO_STOP2 == GPIO_Pin_1);
  CHECK(MARF_GPIO_STROBE2 == GPIO_Pin_14);
#else
  /* v2: direct polarity, high = active. */
  _shim_gpio.IDR = 0;
  CHECK(get_afg1_pulse_inputs().start == 0);
  CHECK(get_afg1_pulse_inputs().stop == 0);
  CHECK(get_afg1_pulse_inputs().strobe == 0);

  _shim_gpio.IDR = MARF_GPIO_START1;
  CHECK(get_afg1_pulse_inputs().start == 1);
  CHECK(get_afg2_pulse_inputs().start == 0);

  _shim_gpio.IDR = MARF_GPIO_STOP2 | MARF_GPIO_STROBE2;
  CHECK(get_afg2_pulse_inputs().stop == 1);
  CHECK(get_afg2_pulse_inputs().strobe == 1);
  CHECK(get_afg1_pulse_inputs().stop == 0);

  /* v2 pin map: STOP1=PB0 STOP2=PB1 STROBE1=PB5 STROBE2=PB7 START1=PB8
   * START2=PB6. */
  CHECK(MARF_GPIO_STOP1 == GPIO_Pin_0);
  CHECK(MARF_GPIO_STOP2 == GPIO_Pin_1);
  CHECK(MARF_GPIO_STROBE1 == GPIO_Pin_5);
  CHECK(MARF_GPIO_STROBE2 == GPIO_Pin_7);
  CHECK(MARF_GPIO_START1 == GPIO_Pin_8);
  CHECK(MARF_GPIO_START2 == GPIO_Pin_6);
#endif

  _shim_gpio.IDR = 0;
}

#if MARF_HW == 1
/* ---- v1 ADC mux mask table -------------------------------------------------
 * The explicit full-chain masks are the wiring truth for v1 boards, extracted
 * from the stock rev 1.0 firmware and verified channel-by-channel on real
 * hardware (v3.2.1 bring-up). ANY edit to the table is presumed wrong until
 * re-verified on hardware: this pins its exact contents with a checksum.
 *
 * Including the .c gives the test access to the static table. */
#include "adc_pots_selector.c"

static void test_v1_mux_mask_table_frozen(void) {
  printf("invariants: v1 mux mask table frozen (FNV checksum)\n");

  uint64_t h = 1469598103934665603ull;               /* FNV-1a 64 */
  for (int i = 0; i < 72; i++) {
    unsigned long long w = v1_ch_sel_data[i];
    for (int b = 0; b < 8; b++) {
      h ^= (w >> (8 * b)) & 0xFF;
      h *= 1099511628211ull;
    }
  }
  /* Pinned from the hardware-verified table (commit 727e20b lineage). If you
   * meant to change the table, re-verify EVERY channel on a real v1 unit
   * before updating this constant. */
  const uint64_t expected = 0x9e1e5db1d2984323ull;
  if (h != expected) printf("  v1 mask table FNV = 0x%016llx\n", (unsigned long long) h);
  CHECK(h == expected);
}
#endif

void run_v1_invariants_tests(void) {
  test_pulse_input_polarity();
#if MARF_HW == 1
  test_v1_mux_mask_table_frozen();
#endif
}
