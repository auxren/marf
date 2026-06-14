#ifndef __STORAGE_H
#define __STORAGE_H

#include <stdint.h>
#include <stddef.h>   // offsetof, for the frozen-format static asserts

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
#define MARF_PROGRAM_VERSION  3   // v3: expanded/reordered scale list (35 scales)
#define MARF_CAL_VERSION      1

// ---- Saved program --------------------------------------------------------
typedef struct {
  uStep      steps[32];
  StepSliders sliders[32];
  uint8_t    scale[2];   // per-AFG quantizer scale
  uint8_t    root[2];    // per-AFG quantizer root
} ProgramPayload;  // 260 bytes

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;        // CRC-16/CCITT over payload
  ProgramPayload payload;
} StoredProgram;

// ---- Calibration ----------------------------------------------------------
typedef struct {
  uint16_t cal_constants[8];
  uint8_t  swapped_pulses;          // swap the Pulse 1/2 LEDs
  uint8_t  swapped_pulse_switches;  // swap Pulse 1/2 switch inputs + output jacks (was reserved)
} CalPayload;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;        // CRC-16/CCITT over payload
  CalPayload payload;
} StoredCal;

// ---------------------------------------------------------------------------
// FROZEN CALIBRATION FORMAT (goal: no recalibration from 3.0 onward).
//
// Calibration lives in the external EEPROM, not in STM32 flash, so reflashing
// the firmware never erases it. On boot it is read back and accepted only if
// its magic, version and CRC all match. For a firmware update from 3.0 onward
// to NEVER force a recalibration, the on-EEPROM calibration record must stay
// byte-identical forever:
//
//   * MARF_STORAGE_MAGIC, MARF_CAL_VERSION and the CRC-16 algorithm are fixed;
//   * sizeof(StoredCal)/CalPayload and the header offsets are fixed. The record
//     is anchored to the EEPROM tail at 0xFFFF - sizeof(StoredCal), so changing
//     its size would relocate it and orphan every existing calibration.
//
// New calibration fields must reuse spare bytes inside the existing 18-byte
// CalPayload (a field added this way reads back 0 = "old default" on records
// written before it existed) WITHOUT changing the struct size or bumping
// MARF_CAL_VERSION, so old and new records keep cross-validating. (The former
// `reserved` byte is now `swapped_pulse_switches`; cal_constants are uint16_t
// but only need 12 bits, leaving headroom there if more flags are needed.)
// Growing the struct or bumping the version is the one
// thing that forces every user to recalibrate -- the asserts below fail the
// build if that contract is broken, so it can only happen deliberately.
//
// (MARF_PROGRAM_VERSION is independent and may bump freely: saved programs live
// at the EEPROM head, far from the calibration tail -- see eprom.c.)
// ---------------------------------------------------------------------------
_Static_assert(MARF_STORAGE_MAGIC == 0x4652414Du,
               "EEPROM magic is frozen from 3.0");
_Static_assert(MARF_CAL_VERSION == 1,
               "calibration format is frozen from 3.0; bumping this forces every user to recalibrate");
_Static_assert(sizeof(CalPayload) == 18,
               "CalPayload size is frozen; repurpose .reserved instead of growing it");
_Static_assert(sizeof(StoredCal) == 28,
               "StoredCal size is frozen; changing it moves the EEPROM tail record and orphans existing calibration");
_Static_assert(offsetof(StoredCal, payload) == 8,
               "StoredCal header layout is frozen");

// CRC-16/CCITT (poly 0x1021, init 0xFFFF). Pure, host-testable.
uint16_t marf_crc16(const void *data, uint32_t len);

// Stamp magic/version and compute the CRC over the current payload.
void marf_stored_program_finalize(StoredProgram *s);
void marf_stored_cal_finalize(StoredCal *s);

// Return 1 if magic, version and CRC all check out.
int marf_stored_program_valid(const StoredProgram *s);
int marf_stored_cal_valid(const StoredCal *s);

#endif
