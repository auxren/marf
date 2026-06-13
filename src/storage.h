#ifndef __STORAGE_H
#define __STORAGE_H

#include <stdint.h>

#include "program.h"  // uStep, StepSliders

// ---------------------------------------------------------------------------
// Versioned, checksummed EEPROM record formats.
//
// Every persisted block carries a magic number, a format version and a CRC-16
// over its payload. On load we verify all three and refuse the block if any
// fail, instead of memcpy-ing raw (possibly garbage or older-format) bytes into
// the live program/calibration state.
//
// Bumping a *_VERSION below intentionally invalidates blocks written by older
// firmware, so they are cleanly ignored rather than silently misinterpreted.
// ---------------------------------------------------------------------------

#define MARF_STORAGE_MAGIC    0x4652414Du  // 'MARF'
#define MARF_PROGRAM_VERSION  1
#define MARF_CAL_VERSION      1

// ---- Saved program --------------------------------------------------------
typedef struct {
  uStep      steps[32];
  StepSliders sliders[32];
} ProgramPayload;  // 256 bytes

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;        // CRC-16/CCITT over payload
  ProgramPayload payload;
} StoredProgram;

// ---- Calibration ----------------------------------------------------------
typedef struct {
  uint16_t cal_constants[8];
  uint8_t  swapped_pulses;
  uint8_t  reserved;
} CalPayload;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;        // CRC-16/CCITT over payload
  CalPayload payload;
} StoredCal;

// CRC-16/CCITT (poly 0x1021, init 0xFFFF). Pure, host-testable.
uint16_t marf_crc16(const void *data, uint32_t len);

// Stamp magic/version and compute the CRC over the current payload.
void marf_stored_program_finalize(StoredProgram *s);
void marf_stored_cal_finalize(StoredCal *s);

// Return 1 if magic, version and CRC all check out.
int marf_stored_program_valid(const StoredProgram *s);
int marf_stored_cal_valid(const StoredCal *s);

#endif
