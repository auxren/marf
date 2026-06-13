#include "scales.h"

// Active selection. Defaults preserve the original chromatic behaviour.
volatile uint8_t current_scale = SCALE_CHROMATIC;
volatile uint8_t current_scale_root = 0;

// 12-bit pitch-class masks (bit i set => semitone i is in the scale).
static const uint16_t SCALE_MASKS[SCALE_COUNT] = {
  [SCALE_CHROMATIC]        = 0x0FFF, // 0 1 2 3 4 5 6 7 8 9 10 11
  [SCALE_MAJOR]            = 0x0AB5, // 0 2 4 5 7 9 11
  [SCALE_NATURAL_MINOR]    = 0x05AD, // 0 2 3 5 7 8 10
  [SCALE_HARMONIC_MINOR]   = 0x09AD, // 0 2 3 5 7 8 11
  [SCALE_DORIAN]           = 0x06AD, // 0 2 3 5 7 9 10
  [SCALE_PHRYGIAN]         = 0x05AB, // 0 1 3 5 7 8 10
  [SCALE_MIXOLYDIAN]       = 0x06B5, // 0 2 4 5 7 9 10
  [SCALE_LYDIAN]           = 0x0AD5, // 0 2 4 6 7 9 11
  [SCALE_PENTATONIC_MAJOR] = 0x0295, // 0 2 4 7 9
  [SCALE_PENTATONIC_MINOR] = 0x04A9, // 0 3 5 7 10
  [SCALE_WHOLE_TONE]       = 0x0555, // 0 2 4 6 8 10
  [SCALE_OCTAVE]           = 0x0001, // 0
};

static const char *const SCALE_NAMES[SCALE_COUNT] = {
  [SCALE_CHROMATIC]        = "Chromatic",
  [SCALE_MAJOR]            = "Major",
  [SCALE_NATURAL_MINOR]    = "Minor",
  [SCALE_HARMONIC_MINOR]   = "Harm Minor",
  [SCALE_DORIAN]           = "Dorian",
  [SCALE_PHRYGIAN]         = "Phrygian",
  [SCALE_MIXOLYDIAN]       = "Mixolydian",
  [SCALE_LYDIAN]           = "Lydian",
  [SCALE_PENTATONIC_MAJOR] = "Pent Maj",
  [SCALE_PENTATONIC_MINOR] = "Pent Min",
  [SCALE_WHOLE_TONE]       = "Whole Tone",
  [SCALE_OCTAVE]           = "Octave",
};

uint16_t scale_mask(uint8_t scale) {
  return (scale < SCALE_COUNT) ? SCALE_MASKS[scale] : 0x0FFF;
}

const char *scale_name(uint8_t scale) {
  return (scale < SCALE_COUNT) ? SCALE_NAMES[scale] : "?";
}

// True if semitone s is in `scale` for `root`.
static int in_scale(uint16_t mask, int root, int s) {
  int pc = ((s - root) % 12 + 12) % 12;   // pitch class relative to root, 0..11
  return (mask >> pc) & 1u;
}

int scale_quantize_semitone(uint8_t scale, int root, int semitone) {
  if (scale >= SCALE_COUNT) return semitone;
  uint16_t mask = SCALE_MASKS[scale];
  if (mask == 0x0FFF) return semitone;      // chromatic: nothing to do

  // Search outward from the input; prefer the higher note on a tie.
  for (int d = 0; d < 12; d++) {
    if (in_scale(mask, root, semitone + d)) return semitone + d;
    if (in_scale(mask, root, semitone - d)) return semitone - d;
  }
  return semitone;                          // unreachable for a non-empty mask
}
