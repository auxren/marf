#ifndef __WATCHDOG_H
#define __WATCHDOG_H

// Independent watchdog (IWDG). Once started it cannot be stopped, and the MCU
// resets if WatchdogRefresh() is not called within the timeout (~3 s nominal,
// clocked by the LSI). It is started after init so that a hang in the normal
// run loop auto-recovers; the refresh lives in ControllerCommonAllLoops(),
// which runs in the main loop and in the load/save modal loops.
void WatchdogInit(void);
void WatchdogRefresh(void);

#endif
