#ifndef __TURING_H
#define __TURING_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Turing-machine style looping shift-register voltage source.
//
// Pure, hardware-free and host-tested. One TuringMachine per stage: a classic
// looping 16-bit shift register, clocked from its assigned external input
// ONLY while the sequencer is on that stage (soft-normalled to once per stage
// entry when nothing is patched). The feedback tap sits at bit (length-1), so
// the bit stream - and therefore the value sequence - repeats with period =
// `length` (2..16). The value is always the low 8 bits read as a number, so
// it spans the full 0..4095 range at any length. Each clock plays the next
// step of the loop; with probability set by `change` (the stage's voltage
// slider, read live) the fed-back bit "slips" (is replaced with a fresh
// random bit), mutating the loop.
//
// Mapping (monotonic, squared "slip" curve), both hardware revisions:
//   change 0 (slider down) : locked -> the same `length` voltages repeat in
//                            sequence, exactly, forever
//   change 4095 (slider up): every clock slips -> the sequence never repeats
//   in between             : mostly repeating, mutating more and more as the
//                            slider rises (squared curve)
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

// Public access to the shared xorshift32 PRNG.
uint32_t marf_rand(void);

// Initialise a machine: clamp length to 2..16 and fill with random bits.
void turing_init(TuringMachine *t, uint8_t length);

// Change the loop length (clamped 2..16), preserving register contents.
void turing_set_length(TuringMachine *t, uint8_t length);

// Advance one clock. `change` is the 12-bit "big knob" value (0..4095).
void turing_clock(TuringMachine *t, uint16_t change);

// Current output value, scaled to 0..4095.
uint16_t turing_value(const TuringMachine *t);

// ---- MARF runtime state ---------------------------------------------------
// One machine per stage (16, or 32 with an expander), plus a per-sequence
// enable flag. Defined in turing.c.
#define TURING_NUM_STAGES 32
extern TuringMachine turing_machines[TURING_NUM_STAGES];
extern volatile uint8_t turing_enabled[2];   // per AFG

// Seed the PRNG and initialise every stage machine (default length).
void turing_machines_init(void);

#endif
