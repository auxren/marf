#include "storage.h"

uint16_t marf_crc16(const void *data, uint32_t len) {
  const uint8_t *p = (const uint8_t *) data;
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= (uint16_t) p[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021) : (uint16_t) (crc << 1);
    }
  }
  return crc;
}

void marf_stored_program_finalize(StoredProgram *s) {
  s->magic = MARF_STORAGE_MAGIC;
  s->version = MARF_PROGRAM_VERSION;
  s->crc = marf_crc16(&s->payload, sizeof(s->payload));
}

void marf_stored_cal_finalize(StoredCal *s) {
  s->magic = MARF_STORAGE_MAGIC;
  s->version = MARF_CAL_VERSION;
  s->crc = marf_crc16(&s->payload, sizeof(s->payload));
}

int marf_stored_program_valid(const StoredProgram *s) {
  if (s->magic != MARF_STORAGE_MAGIC) return 0;
  if (s->version != MARF_PROGRAM_VERSION) return 0;
  return s->crc == marf_crc16(&s->payload, sizeof(s->payload));
}

int marf_stored_cal_valid(const StoredCal *s) {
  if (s->magic != MARF_STORAGE_MAGIC) return 0;
  if (s->version != MARF_CAL_VERSION) return 0;
  return s->crc == marf_crc16(&s->payload, sizeof(s->payload));
}

void marf_stored_twopoint_finalize(StoredTwoPointCal *s) {
  s->magic = MARF_STORAGE_MAGIC;
  s->version = MARF_TWOPOINT_VERSION;
  s->crc = marf_crc16(&s->payload, sizeof(s->payload));
}

int marf_stored_twopoint_valid(const StoredTwoPointCal *s) {
  if (s->magic != MARF_STORAGE_MAGIC) return 0;
  if (s->version != MARF_TWOPOINT_VERSION) return 0;
  return s->crc == marf_crc16(&s->payload, sizeof(s->payload));
}
