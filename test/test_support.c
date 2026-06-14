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
