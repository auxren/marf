/*
 * Host unit tests for the versioned/checksummed EEPROM record formats.
 */
#include <stdio.h>
#include <string.h>

#include "storage.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_storage_tests(void);

static void test_crc16_known(void) {
  printf("test_crc16_known\n");
  /* CRC-16/CCITT-FALSE of "123456789" is 0x29B1 (standard check value). */
  CHECK(marf_crc16("123456789", 9) == 0x29B1);
  /* Deterministic + sensitive to a single-bit change. */
  CHECK(marf_crc16("hello", 5) == marf_crc16("hello", 5));
  CHECK(marf_crc16("hello", 5) != marf_crc16("hellp", 5));
}

static void test_layout_sizes(void) {
  printf("test_layout_sizes\n");
  CHECK(sizeof(ProgramPayload) == 256);   /* 32*4 steps + 32*4 sliders */
  CHECK(sizeof(StoredProgram) == 264);    /* 8-byte header + payload */
}

static void test_program_roundtrip(void) {
  printf("test_program_roundtrip\n");
  StoredProgram s;
  memset(&s, 0, sizeof(s));
  s.payload.sliders[3].VLevel = 1234;
  s.payload.steps[7].b.Quantize = 1;
  marf_stored_program_finalize(&s);

  CHECK(marf_stored_program_valid(&s) == 1);

  /* Corrupt a payload byte -> CRC mismatch -> invalid */
  s.payload.sliders[3].VLevel ^= 0x0100;
  CHECK(marf_stored_program_valid(&s) == 0);
  s.payload.sliders[3].VLevel ^= 0x0100;        /* restore */
  CHECK(marf_stored_program_valid(&s) == 1);

  /* Wrong magic (e.g. blank/old EEPROM) -> invalid */
  s.magic = 0;
  CHECK(marf_stored_program_valid(&s) == 0);
  marf_stored_program_finalize(&s);

  /* Wrong version (older/newer format) -> invalid */
  s.version = MARF_PROGRAM_VERSION + 1;
  CHECK(marf_stored_program_valid(&s) == 0);
}

static void test_cal_roundtrip(void) {
  printf("test_cal_roundtrip\n");
  StoredCal c;
  memset(&c, 0, sizeof(c));
  for (int i = 0; i < 8; i++) c.payload.cal_constants[i] = 4000 + i;
  c.payload.swapped_pulses = 1;
  marf_stored_cal_finalize(&c);

  CHECK(marf_stored_cal_valid(&c) == 1);
  c.payload.cal_constants[2]++;
  CHECK(marf_stored_cal_valid(&c) == 0);   /* corruption caught */
}

void run_storage_tests(void) {
  test_crc16_known();
  test_layout_sizes();
  test_program_roundtrip();
  test_cal_roundtrip();
}
