#include "scales.h"

// Active selection per sequence. Defaults preserve chromatic behaviour.
volatile uint8_t afg_scale[2] = { SCALE_CHROMATIC, SCALE_CHROMATIC };
volatile uint8_t afg_root[2]  = { 0, 0 };

// A dead band at each end so the extremes are reliably reachable even when a
// slider doesn't quite sweep 0..4095 (uncalibrated travel, mechanical slop):
// the bottom band always selects the first item, the top band the last.
#define SLIDER_SELECT_MARGIN 250u   // ~6% at each end

static uint8_t select_from_slider(uint16_t level, uint8_t count) {
  if (level <= SLIDER_SELECT_MARGIN) return 0;
  if (level >= 4095u - SLIDER_SELECT_MARGIN) return count - 1;
  uint32_t span = 4096u - 2u * SLIDER_SELECT_MARGIN;
  uint8_t i = (uint8_t) (((uint32_t) (level - SLIDER_SELECT_MARGIN) * count) / span);
  return (i < count) ? i : (count - 1);
}

uint8_t scale_from_slider(uint16_t level) {
  return select_from_slider(level, SCALE_COUNT);   // bottom = Chromatic
}

uint8_t root_from_slider(uint16_t level) {
  return select_from_slider(level, 12);            // bottom = root 0
}

// 12-bit pitch-class masks (bit i set => semitone i, relative to root, is in
// the scale). Intervals per the on-panel scale list.
static const uint16_t SCALE_MASKS[SCALE_COUNT] = {
  [SCALE_CHROMATIC]             = 0x0FFF, // 1 2b 2 3b 3 4 5b 5 6b 6 7b 7
  [SCALE_MAJOR]                 = 0x0AB5, // 1 2 3 4 5 6 7 (Ionian)
  [SCALE_DORIAN]                = 0x06AD, // 1 2 3b 4 5 6 7b
  [SCALE_PHRYGIAN]              = 0x05AB, // 1 2b 3b 4 5 6b 7b
  [SCALE_LYDIAN]                = 0x0AD5, // 1 2 3 4# 5 6 7
  [SCALE_MIXOLYDIAN]            = 0x06B5, // 1 2 3 4 5 6 7b
  [SCALE_NATURAL_MINOR]         = 0x05AD, // 1 2 3b 4 5 6b 7b (Aeolian)
  [SCALE_LOCRIAN]               = 0x056B, // 1 2b 3b 4 5b 6b 7b
  [SCALE_HARMONIC_MINOR]        = 0x09AD, // 1 2 3b 4 5 6b 7
  [SCALE_MELODIC_MINOR]         = 0x0AAD, // 1 2 3b 4 5 6 7
  [SCALE_MAJOR_BLUES]           = 0x029D, // 1 2 3b 3 5 6
  [SCALE_MINOR_BLUES]           = 0x04E9, // 1 3b 4 5b 5 7b
  [SCALE_DIMINISHED]            = 0x0B6D, // 1 2 3b 4 4# 5# 6 7
  [SCALE_COMBINATION_DIMINISHED]= 0x06DB, // 1 2b 3b 3 4# 5 6 7b
  [SCALE_PENTATONIC_MAJOR]      = 0x0295, // 1 2 3 5 6
  [SCALE_PENTATONIC_MINOR]      = 0x04A9, // 1 3b 4 5 7b
  [SCALE_RAGA_BHAIRAV]          = 0x09B3, // 1 2b 3 4 5 6b 7
  [SCALE_RAGA_GAMANASRAMA]      = 0x0AD3, // 1 2b 3 4# 5 6 7
  [SCALE_RAGA_TODI]             = 0x09CB, // 1 2b 3b 4# 5 6b 7
  [SCALE_ARABIAN]               = 0x0575, // 1 2 3 4 5b 6b 7b
  [SCALE_SPANISH]               = 0x05BB, // 1 2b 3b 3 4 5 6b 7b
  [SCALE_GYPSY]                 = 0x09CD, // 1 2 3b 4# 5 6b 7
  [SCALE_EGYPTIAN]              = 0x04A5, // 1 2 4 5 7b
  [SCALE_HAWAIIAN]              = 0x028D, // 1 2 3b 5 6
  [SCALE_BALINESE_PELOG]        = 0x018B, // 1 2b 3b 5 6b
  [SCALE_JAPANESE_MIYAKOBUSHI]  = 0x01A3, // 1 2b 4 5 6b
  [SCALE_RYUKU]                 = 0x08B1, // 1 3 4 5 7
  [SCALE_CHINESE]               = 0x08C5, // 1 2 4# 5 7
  [SCALE_BASS_LINE]             = 0x0481, // 1 5 7b
  [SCALE_WHOLE_TONE]            = 0x0555, // 1 2 3 5b 6b 7b
  [SCALE_MINOR_3RD]             = 0x0249, // 1 3b 5b 6
  [SCALE_MAJOR_3RD]             = 0x0111, // 1 3 6b
  [SCALE_4TH]                   = 0x0421, // 1 4 7b
  [SCALE_5TH]                   = 0x0081, // 1 5
  [SCALE_OCTAVE]                = 0x0001, // 1
};

static const char *const SCALE_NAMES[SCALE_COUNT] = {
  [SCALE_CHROMATIC]             = "Chromatic",
  [SCALE_MAJOR]                 = "Ionian",
  [SCALE_DORIAN]                = "Dorian",
  [SCALE_PHRYGIAN]              = "Phrygian",
  [SCALE_LYDIAN]                = "Lydian",
  [SCALE_MIXOLYDIAN]            = "Mixolydian",
  [SCALE_NATURAL_MINOR]         = "Aeolian",
  [SCALE_LOCRIAN]               = "Locrian",
  [SCALE_HARMONIC_MINOR]        = "Harm Minor",
  [SCALE_MELODIC_MINOR]         = "Mel Minor",
  [SCALE_MAJOR_BLUES]           = "Major Blues",
  [SCALE_MINOR_BLUES]           = "Minor Blues",
  [SCALE_DIMINISHED]            = "Diminished",
  [SCALE_COMBINATION_DIMINISHED]= "Comb Dim",
  [SCALE_PENTATONIC_MAJOR]      = "Pent Maj",
  [SCALE_PENTATONIC_MINOR]      = "Pent Min",
  [SCALE_RAGA_BHAIRAV]          = "Raga Bhairav",
  [SCALE_RAGA_GAMANASRAMA]      = "Raga Gamanasrama",
  [SCALE_RAGA_TODI]             = "Raga Todi",
  [SCALE_ARABIAN]               = "Arabian",
  [SCALE_SPANISH]               = "Spanish",
  [SCALE_GYPSY]                 = "Gypsy",
  [SCALE_EGYPTIAN]              = "Egyptian",
  [SCALE_HAWAIIAN]              = "Hawaiian",
  [SCALE_BALINESE_PELOG]        = "Balinese Pelog",
  [SCALE_JAPANESE_MIYAKOBUSHI]  = "Japanese Miyakobushi",
  [SCALE_RYUKU]                 = "Ryuku",
  [SCALE_CHINESE]               = "Chinese",
  [SCALE_BASS_LINE]             = "Bass Line",
  [SCALE_WHOLE_TONE]            = "Whole Tone",
  [SCALE_MINOR_3RD]             = "Minor 3rd",
  [SCALE_MAJOR_3RD]             = "Major 3rd",
  [SCALE_4TH]                   = "4th",
  [SCALE_5TH]                   = "5th",
  [SCALE_OCTAVE]                = "Octave",
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
