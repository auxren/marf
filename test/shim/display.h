/*
 * Host shim for display.h.
 *
 * afg.c only needs the display-update flag setters; the real header drags in
 * the LED driver stack. The flag variable itself lives in test_support.c so
 * the virtual bench can assert on display-update requests if it wants to.
 */
#ifndef SHIM_DISPLAY_H
#define SHIM_DISPLAY_H

#include <stdint.h>

typedef union {
  struct {
    unsigned char MainDisplay:1;
    unsigned char StepsDisplay:1;
    unsigned char OT1:1;
    unsigned char OT2:1;
    unsigned char OT3:1;
    unsigned char OT4:1;
    unsigned char OT5:1;
    unsigned char OT6:1;
  } b;
  unsigned char value;
} uDisplayUpdateFlag;

extern volatile uDisplayUpdateFlag display_update_flags;
extern uint32_t steps_leds_lit;

static inline void update_display() {
  display_update_flags.b.MainDisplay = 1;
  display_update_flags.b.StepsDisplay = 1;
}

static inline void update_main_display() {
  display_update_flags.b.MainDisplay = 1;
}

#endif /* SHIM_DISPLAY_H */
