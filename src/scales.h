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
// The active scale/root are per sequence (AFG): afg_scale[0/1], afg_root[0/1].
// They are selected on the panel by holding the Quantize switch and moving a
// slider (voltage -> scale, time -> root) and are saved with the program.
// ---------------------------------------------------------------------------

// Order matches the on-panel scale list (1-based number = enum + 1). Some
// legacy aliases (SCALE_MAJOR = Ionian, SCALE_NATURAL_MINOR = Aeolian) are kept
// for the host tests.
typedef enum {
  SCALE_CHROMATIC = 0,           //  1
  SCALE_MAJOR,                   //  2  Ionian
  SCALE_DORIAN,                  //  3
  SCALE_PHRYGIAN,                //  4
  SCALE_LYDIAN,                  //  5
  SCALE_MIXOLYDIAN,              //  6
  SCALE_NATURAL_MINOR,           //  7  Aeolian
  SCALE_LOCRIAN,                 //  8
  SCALE_HARMONIC_MINOR,          //  9
  SCALE_MELODIC_MINOR,           // 10
  SCALE_MAJOR_BLUES,             // 11
  SCALE_MINOR_BLUES,             // 12
  SCALE_DIMINISHED,              // 13
  SCALE_COMBINATION_DIMINISHED,  // 14
  SCALE_PENTATONIC_MAJOR,        // 15
  SCALE_PENTATONIC_MINOR,        // 16
  SCALE_RAGA_BHAIRAV,            // 17
  SCALE_RAGA_GAMANASRAMA,        // 18
  SCALE_RAGA_TODI,               // 19
  SCALE_ARABIAN,                 // 20
  SCALE_SPANISH,                 // 21
  SCALE_GYPSY,                   // 22
  SCALE_EGYPTIAN,                // 23
  SCALE_HAWAIIAN,                // 24
  SCALE_BALINESE_PELOG,          // 25
  SCALE_JAPANESE_MIYAKOBUSHI,    // 26
  SCALE_RYUKU,                   // 27
  SCALE_CHINESE,                 // 28
  SCALE_BASS_LINE,               // 29
  SCALE_WHOLE_TONE,              // 30
  SCALE_MINOR_3RD,               // 31
  SCALE_MAJOR_3RD,               // 32
  SCALE_4TH,                     // 33
  SCALE_5TH,                     // 34
  SCALE_OCTAVE,                  // 35
  SCALE_COUNT
} ScaleId;

// Active selection per sequence/AFG (defaults: chromatic, root 0).
extern volatile uint8_t afg_scale[2];   // each 0 .. SCALE_COUNT-1
extern volatile uint8_t afg_root[2];    // each 0 .. 11

// Map a 12-bit slider level (0..4095) to a scale index / root.
// Slider at minimum selects Chromatic / root 0.
uint8_t scale_from_slider(uint16_t level);
uint8_t root_from_slider(uint16_t level);

// Snap a chromatic semitone index to the nearest semitone that is in `scale`
// for the given `root` (0..11). On a tie the higher note is chosen. Returns
// the input unchanged for SCALE_CHROMATIC or an out-of-range scale id.
int scale_quantize_semitone(uint8_t scale, int root, int semitone);

// The 12-bit pitch-class mask for a scale (for tests / a future UI).
uint16_t scale_mask(uint8_t scale);

// Short human-readable name (for a future display).
const char *scale_name(uint8_t scale);

#endif
