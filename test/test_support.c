/*
 * Host-side support definitions for the unit tests.
 * Provides the handful of symbols the firmware expects from hardware/other
 * translation units that we deliberately don't compile for host tests.
 */
#include "stm32f4xx.h"

/* Backing store for the shim GPIO pointer(s). */
GPIO_TypeDef _shim_gpio;

/* expander.c is not compiled for host tests (it pulls in dip_config/hardware),
 * so provide the one symbol the logic needs. Tests can set this directly. */
uint8_t has_expander = 0;

/* delays.c is not compiled for host tests; provide the millis counter that
 * get_millis() reads (used by SenseExternalInputs in analog_data.c). */
volatile uint32_t millis = 0;

/* ---- Fake EEPROM for presets.c -------------------------------------------
 * presets.c (PopulateFactoryPresets) talks to the CAT25512 EEPROM and reads
 * eprom_memory. eprom.c / CAT25512.c are hardware and not compiled for host
 * tests, so back them with a plain 64 KB array the tests can inspect. */
#include "eprom.h"

EpromMemory eprom_memory = {};
unsigned char fake_eeprom[0x10000];

void CAT25512_read_block(uint16_t address, uint8_t *data, uint16_t size) {
  for (uint16_t i = 0; i < size; i++) data[i] = fake_eeprom[(uint16_t)(address + i)];
}
void CAT25512_write_block(uint16_t address, uint8_t *data, uint16_t size) {
  for (uint16_t i = 0; i < size; i++) fake_eeprom[(uint16_t)(address + i)] = data[i];
}
