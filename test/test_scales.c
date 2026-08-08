/*
 * Host unit tests for the scale quantizer engine (pure logic).
 */
#include <stdio.h>

#include "scales.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_scales_tests(void);

static int q(uint8_t scale, int root, int s) {
  return scale_quantize_semitone(scale, root, s);
}

static void test_masks(void) {
  printf("test_masks\n");
  CHECK(SCALE_COUNT == 35);
  CHECK(scale_mask(SCALE_CHROMATIC) == 0x0FFF);
  CHECK(scale_mask(SCALE_MAJOR) == 0x0AB5);
  CHECK(scale_mask(SCALE_OCTAVE) == 0x0001);
  /* spot-check several of the expanded scales */
  CHECK(scale_mask(SCALE_WHOLE_TONE) == 0x0555);
  CHECK(scale_mask(SCALE_DIMINISHED) == 0x0B6D);
  CHECK(scale_mask(SCALE_RAGA_TODI) == 0x09CB);
  CHECK(scale_mask(SCALE_5TH) == 0x0081);
  /* every scale must include the root (semitone 0) */
  for (uint8_t sc = 0; sc < SCALE_COUNT; sc++) CHECK(scale_mask(sc) & 1u);
}

static void test_chromatic_identity(void) {
  printf("test_chromatic_identity\n");
  for (int s = -13; s <= 13; s++) CHECK(q(SCALE_CHROMATIC, 0, s) == s);
}

static void test_major_root0(void) {
  printf("test_major_root0\n");
  /* C major {0,2,4,5,7,9,11}; ties resolve upward */
  CHECK(q(SCALE_MAJOR, 0, 0) == 0);
  CHECK(q(SCALE_MAJOR, 0, 1) == 2);   /* tie 0/2 -> up */
  CHECK(q(SCALE_MAJOR, 0, 3) == 4);   /* tie 2/4 -> up */
  CHECK(q(SCALE_MAJOR, 0, 5) == 5);
  CHECK(q(SCALE_MAJOR, 0, 6) == 7);   /* tie 5/7 -> up */
  CHECK(q(SCALE_MAJOR, 0, 11) == 11);
  CHECK(q(SCALE_MAJOR, 0, 12) == 12); /* next octave root */
  CHECK(q(SCALE_MAJOR, 0, 13) == 14);
  CHECK(q(SCALE_MAJOR, 0, -1) == -1); /* B below: pitch class 11 is in scale */
}

static void test_root_shift(void) {
  printf("test_root_shift\n");
  /* D major (root 2): 2 in scale; 3 -> 4 */
  CHECK(q(SCALE_MAJOR, 2, 2) == 2);
  CHECK(q(SCALE_MAJOR, 2, 3) == 4);
}

static void test_pentatonic_and_octave(void) {
  printf("test_pentatonic_and_octave\n");
  /* Pent major {0,2,4,7,9}: 5 -> 4 (down 1 beats up 2) */
  CHECK(q(SCALE_PENTATONIC_MAJOR, 0, 5) == 4);
  CHECK(q(SCALE_PENTATONIC_MAJOR, 0, 6) == 7);
  /* Octave {0}: snap to nearest multiple of 12 */
  CHECK(q(SCALE_OCTAVE, 0, 5) == 0);
  CHECK(q(SCALE_OCTAVE, 0, 7) == 12);
  CHECK(q(SCALE_OCTAVE, 0, 18) == 24);
}

static void test_every_scale_nonempty(void) {
  printf("test_every_scale_nonempty\n");
  /* Every scale must map every input to a note that is actually in the scale. */
  for (uint8_t sc = 0; sc < SCALE_COUNT; sc++) {
    uint16_t mask = scale_mask(sc);
    for (int s = -5; s <= 20; s++) {
      int out = q(sc, 0, s);
      int pc = ((out % 12) + 12) % 12;
      CHECK((mask >> pc) & 1u);
    }
  }
}

static void test_hysteresis_fresh_matches_plain(void) {
  printf("test_hysteresis_fresh_matches_plain\n");
  /* With no note latched (state 0), the hyst variant must round to nearest
     and scale-snap exactly like the plain quantizer. */
  for (int tenths = 0; tenths <= 1200; tenths += 3) {
    float pos = tenths / 10.0f;
    int16_t state = 0;
    int plain = scale_quantize_semitone(SCALE_MAJOR, 0, (int) (pos + 0.5f));
    CHECK(scale_quantize_semitone_hyst(SCALE_MAJOR, 0, pos, &state) == plain);
    CHECK(state != 0);
  }
}

static void test_hysteresis_holds_at_boundary(void) {
  printf("test_hysteresis_holds_at_boundary\n");
  /* A stage parked on the 33/34 boundary with ADC dither must not flap. */
  int16_t state = 0;
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.40f, &state) == 33);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.52f, &state) == 33);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.48f, &state) == 33);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.60f, &state) == 33);
  /* ...and the same parked just past the boundary, latched high. */
  state = 0;
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.60f, &state) == 34);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.45f, &state) == 34);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.55f, &state) == 34);
}

static void test_hysteresis_deliberate_move_switches(void) {
  printf("test_hysteresis_deliberate_move_switches\n");
  /* Sweeping past boundary + guard band must switch, in both directions. */
  int16_t state = 0;
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.00f, &state) == 33);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.70f, &state) == 34);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 33.30f, &state) == 33);
  /* A big jump (new step, preset recall, turing value) lands immediately. */
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 59.40f, &state) == 59);
  CHECK(scale_quantize_semitone_hyst(SCALE_CHROMATIC, 0, 12.10f, &state) == 12);
}

static void test_hysteresis_with_scale_snap(void) {
  printf("test_hysteresis_with_scale_snap\n");
  /* Hysteresis acts on the chromatic index; the scale snap still applies.
     C major: chromatic 6 snaps up to 7. */
  int16_t state = 0;
  CHECK(scale_quantize_semitone_hyst(SCALE_MAJOR, 0, 5.90f, &state) == 7);
  /* Dither around the 5/6 chromatic boundary keeps the latched index 6. */
  CHECK(scale_quantize_semitone_hyst(SCALE_MAJOR, 0, 5.55f, &state) == 7);
  /* Well below the boundary: chromatic 5 is in scale. */
  CHECK(scale_quantize_semitone_hyst(SCALE_MAJOR, 0, 5.30f, &state) == 5);
}

void run_scales_tests(void) {
  test_masks();
  test_chromatic_identity();
  test_major_root0();
  test_root_shift();
  test_pentatonic_and_octave();
  test_every_scale_nonempty();
  test_hysteresis_fresh_matches_plain();
  test_hysteresis_holds_at_boundary();
  test_hysteresis_deliberate_move_switches();
  test_hysteresis_with_scale_snap();
}
