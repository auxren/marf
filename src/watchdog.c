#include "watchdog.h"

#include <stm32f4xx.h>

// IWDG runs from the LSI (~32 kHz, but spec'd 17-47 kHz, so the timeout is
// approximate). Prescaler /256 -> ~8 ms per count; reload 375 -> ~3 s nominal
// (~2 s with a fast LSI, ~5.6 s with a slow one). The normal run loop iterates
// far faster than this, and the only multi-second blocking op (full-chip erase
// during calibration) runs before the watchdog is started.
#define WATCHDOG_RELOAD 375

void WatchdogInit(void) {
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_256);
  IWDG_SetReload(WATCHDOG_RELOAD);
  IWDG_ReloadCounter();
  IWDG_Enable();
}

void WatchdogRefresh(void) {
  IWDG_ReloadCounter();
}
