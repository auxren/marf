#include "turing.h"

// Hardware revision (v2 default). Kept as a plain define so this file stays
// free of hardware headers and host-testable (the host tests exercise the v2
// behaviour).
#ifndef MARF_HW
#define MARF_HW 2
#endif

// Shared xorshift32 PRNG. Deterministic given a seed (so the host tests are
// reproducible); on the target it is seeded once from the cycle counter.
static uint32_t rng_state = 0x1234567u;

void turing_seed(uint32_t seed) {
  rng_state = seed ? seed : 0xA5A5A5A5u;
}

static uint32_t rng_next(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

// Public access to the shared PRNG (used by the randomize feature).
uint32_t marf_rand(void) {
  return rng_next();
}

void turing_set_length(TuringMachine *t, uint8_t length) {
  if (length < TURING_MIN_LENGTH) length = TURING_MIN_LENGTH;
  if (length > TURING_MAX_LENGTH) length = TURING_MAX_LENGTH;
  t->length = length;
#if MARF_HW == 1
  // v1 (frozen): the register is truncated to the loop length.
  t->bits &= (uint16_t) ((1u << length) - 1u);
#endif
  // v2: all 16 bits are kept - the length only moves the feedback tap, and
  // the upper bits carry the stream history the 8-bit read window needs.
}

void turing_init(TuringMachine *t, uint8_t length) {
  t->bits = 0;
  turing_set_length(t, length);
#if MARF_HW == 1
  t->bits = (uint16_t) (rng_next() & ((1u << t->length) - 1u));
#else
  t->bits = (uint16_t) rng_next();   // fill the whole 16-bit register
#endif
}

#if MARF_HW == 1

// ---- v1 (legacy) mapping: unchanged from the earlier firmware --------------
// Zero at both extremes (locked / inverted double-lock), highest at centre
// (random), concentrated near centre so the mid-range "slips".
static uint16_t change_to_prand(uint16_t change) {
  int d = 2048 - (int) change;          // distance from centre, signed
  if (d < 0) d = -d;                     // 0 (centre) .. 2048 (extreme)
  int closeness = 2048 - d;              // 2048 (centre) .. 0 (extreme)
  // square to concentrate randomness near the centre
  return (uint16_t) (((closeness * closeness) / 2048) * 2);  // ~0..4096
}

void turing_clock(TuringMachine *t, uint16_t change) {
  uint8_t n = t->length;
  uint8_t outgoing = (uint8_t) ((t->bits >> (n - 1)) & 1u);
  uint8_t new_bit;

  if ((rng_next() & 0x0FFFu) < change_to_prand(change)) {
    new_bit = (uint8_t) (rng_next() & 1u);   // mutate: fresh random bit
  } else if (change >= 2048) {
    new_bit = outgoing;                       // clockwise: copy  -> loop
  } else {
    new_bit = (uint8_t) (outgoing ^ 1u);      // counter-cw: invert -> double loop
  }

  t->bits = (uint16_t) (((t->bits << 1) | new_bit) & ((1u << n) - 1u));
}

#else

// ---- v2: classic looping shift register with a monotonic "slip" curve ------
// The register is always 16 bits; the feedback tap sits at bit (length-1), so
// the recirculating bit stream repeats with period = length. The VALUE is
// always the low 8 bits read as a number - full 0..4095 range at any length.
// Every clock shifts the register (the loop is played BY the clock); the
// slider sets the probability that the fed-back bit "slips" (is replaced with
// a fresh random bit):
//
//   bottom (0)    -> 0     : locked - the same `length` voltages repeat, in
//                            sequence, forever
//   top (4095)    -> 4096  : every clock slips - the sequence never repeats
//   in between    -> squared curve, so most of the travel is gentle
//                    mutation of the loop, ramping up steeply near the top
#define TURING_LOCK_ZONE 200   // bottom dead-zone: a solid lock is easy to hit

static uint16_t change_to_prand(uint16_t change) {
  if (change <= TURING_LOCK_ZONE) return 0;
  uint32_t x = ((uint32_t) (change - TURING_LOCK_ZONE) * 4096u)
               / (4095u - TURING_LOCK_ZONE);   // 0..4096 linear
  return (uint16_t) ((x * x) >> 12);           // squared: 0..4096
}

void turing_clock(TuringMachine *t, uint16_t change) {
  uint8_t n = t->length;
  uint8_t new_bit = (uint8_t) ((t->bits >> (n - 1)) & 1u);   // loop tap
  if ((rng_next() & 0x0FFFu) < change_to_prand(change)) {
    new_bit = (uint8_t) (rng_next() & 1u);                    // slip
  }
  t->bits = (uint16_t) ((t->bits << 1) | new_bit);            // keep 16 bits
}

#endif  // MARF_HW

uint16_t turing_value(const TuringMachine *t) {
  // Read the low 8 bits (like the Turing "Volts" expander) and scale to 12-bit.
  uint16_t v = (uint16_t) (t->bits & 0xFFu);
  return (uint16_t) (((uint32_t) v * 4095u) / 255u);
}

// ---- MARF runtime state ---------------------------------------------------
TuringMachine turing_machines[TURING_NUM_STAGES];
volatile uint8_t turing_enabled[2] = { 0, 0 };

void turing_machines_init(void) {
  turing_seed(0xC0FFEEu);   // re-seeded from the cycle counter by the caller if desired
  for (uint8_t i = 0; i < TURING_NUM_STAGES; i++) {
    turing_init(&turing_machines[i], 8);
  }
}
