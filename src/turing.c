#include "turing.h"

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

void turing_set_length(TuringMachine *t, uint8_t length) {
  if (length < TURING_MIN_LENGTH) length = TURING_MIN_LENGTH;
  if (length > TURING_MAX_LENGTH) length = TURING_MAX_LENGTH;
  t->length = length;
  t->bits &= (uint16_t) ((1u << length) - 1u);
}

void turing_init(TuringMachine *t, uint8_t length) {
  t->bits = 0;
  turing_set_length(t, length);
  t->bits = (uint16_t) (rng_next() & ((1u << t->length) - 1u));
}

// Map the 12-bit "big knob" to a probability (0..4096) of writing a fresh
// random bit. Zero at both extremes (locked), highest at centre (random), and
// concentrated near centre so the mid-range "slips" rather than scrambles.
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
