#ifndef __SCALES_H
#define __SCALES_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Musical scale quantization.
//
// Pure, hardware-free and host-tested. Works in chromatic *semitone index*
// space (an integer count of semitones from the 0 V reference), so it is
// independent of the volts/octave setting -- the caller multiplies the result
// by the per-semitone DAC span (semitone_offset).
//
// A scale is a 12-bit pitch-class mask (bit i set => semitone i, relative to
// the root, is in the scale). SCALE_CHROMATIC keeps every semitone, so it is
// behaviourally identical to the original quantizer and is the default.
//
// The active scale/root live in `current_scale` / `current_scale_root`. The
// selection UI is deliberately not wired yet; set these to drive it. Persisting
// the selection later means adding a field to the saved-program payload and
// bumping MARF_PROGRAM_VERSION (the EEPROM format is already versioned).
// ---------------------------------------------------------------------------

typedef enum {
  SCALE_CHROMATIC = 0,
  SCALE_MAJOR,
  SCALE_NATURAL_MINOR,
  SCALE_HARMONIC_MINOR,
  SCALE_DORIAN,
  SCALE_PHRYGIAN,
  SCALE_MIXOLYDIAN,
  SCALE_LYDIAN,
  SCALE_PENTATONIC_MAJOR,
  SCALE_PENTATONIC_MINOR,
  SCALE_WHOLE_TONE,
  SCALE_OCTAVE,
  SCALE_COUNT
} ScaleId;

// Active selection (defaults: chromatic, root 0 = the 0 V reference).
extern volatile uint8_t current_scale;       // 0 .. SCALE_COUNT-1
extern volatile uint8_t current_scale_root;  // 0 .. 11

// Snap a chromatic semitone index to the nearest semitone that is in `scale`
// for the given `root` (0..11). On a tie the higher note is chosen. Returns
// the input unchanged for SCALE_CHROMATIC or an out-of-range scale id.
int scale_quantize_semitone(uint8_t scale, int root, int semitone);

// The 12-bit pitch-class mask for a scale (for tests / a future UI).
uint16_t scale_mask(uint8_t scale);

// Short human-readable name (for a future display).
const char *scale_name(uint8_t scale);

#endif
