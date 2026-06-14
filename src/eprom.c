#include "eprom.h"

#include <stdint.h>

#include "program.h"
#include "analog_data.h"
#include "display.h"
#include "storage.h"

// 64kb available in eprom memory
EpromMemory eprom_memory = {};

// The saved-program region grows from the EEPROM head; the calibration record
// is anchored to the tail. Guarantee they never collide, so a future program
// format (more slots or a larger StoredProgram) can never overwrite calibration
// -- which keeps the "no recalibration from 3.0 on" promise intact even as the
// program format evolves. Fails the build if the head region reaches the tail.
_Static_assert(16 * sizeof(StoredProgram) <= (0xFFFFu - sizeof(StoredCal)),
               "saved-program region overlaps the calibration record at the EEPROM tail");

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
