#ifndef __EPROM_H
#define __EPROM_H

#include <stdint.h>

#include "CAT25512.h"
#include "program.h"
#include "storage.h"

// Typedef for one section of eprom memory layout
typedef struct {
  uint16_t start;
  uint16_t size;
} MemoryRange;

// Typedef for full eprom memory layout
typedef struct {
  // 16 saved programs (each a versioned, checksummed StoredProgram)
  MemoryRange programs[16];

  // Analog calibration data + pulse-led-swap flag (versioned StoredCal)
  MemoryRange analog_cal_data;
} EpromMemory;

extern EpromMemory eprom_memory;

void EpromInitializeMemoryLayout();

#endif
