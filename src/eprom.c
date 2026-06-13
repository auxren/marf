#include "eprom.h"

#include <stdint.h>

#include "program.h"
#include "analog_data.h"
#include "display.h"

// 64kb available in eprom memory
EpromMemory eprom_memory = {};

void EpromInitializeMemoryLayout() {
  volatile uint16_t start = 0, size = 0; // in bytes

  // Saved programs at the head
  size = sizeof(StoredProgram);
  for (uint8_t p = 0; p < 16; p++) {
    eprom_memory.programs[p].start = start;
    eprom_memory.programs[p].size = size;
    start += size;
  }

  // Calibration data (constants + pulse-led-swap flag) at the tail
  start = 0xFFFF;
  size = sizeof(StoredCal);
  start -= size;
  eprom_memory.analog_cal_data.start = start;
  eprom_memory.analog_cal_data.size = size;
}
