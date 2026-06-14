#ifndef __TURING_H
#define __TURING_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Turing-machine style looping shift-register voltage source.
//
// Pure, hardware-free and host-tested. One TuringMachine per stage: a circular
// shift register of programmable length (2..16). On each clock the bit that
// wraps around is fed back, copied / inverted / randomised according to a
// "change" amount (the Music Thing "big knob"):
//
//   change far clockwise  (-> 4095): copy  -> the loop repeats   (period = len)
//   change centre         (~ 2048): random -> never repeats
//   change far counter-cw (-> 0)   : invert -> "double lock"     (period = 2*len)
//   in between                     : "slip" - mostly looping, occasional change
//
// The output is the low bits of the register read as a number and scaled to the
// 12-bit 0..4095 range used everywhere else (the caller then applies the step's
// range / quantize / scale on top, exactly as for a real external input).
// ---------------------------------------------------------------------------

#define TURING_MIN_LENGTH 2
#define TURING_MAX_LENGTH 16

typedef struct {
  uint16_t bits;    // shift register contents (low `length` bits used)
  uint8_t  length;  // active length, 2..16
} TuringMachine;

// Seed the shared PRNG (e.g. from the DWT cycle counter at boot).
void turing_seed(uint32_t seed);

// Initialise a machine: clamp length to 2..16 and fill with random bits.
void turing_init(TuringMachine *t, uint8_t length);

// Change the loop length (clamped 2..16), preserving register contents.
void turing_set_length(TuringMachine *t, uint8_t length);

// Advance one clock. `change` is the 12-bit "big knob" value (0..4095).
void turing_clock(TuringMachine *t, uint16_t change);

// Current output value, scaled to 0..4095.
uint16_t turing_value(const TuringMachine *t);

#endif
