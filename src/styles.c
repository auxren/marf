#include "styles.h"
#include "scales.h"

// Roster of style presets. Degrees are semitones relative to the style's root;
// quantize snaps them into the scale, so they only need to be close.
const Style styles[] = {
  { // 0 - Suzanne Ciani: her Vertical Sequencer, a C6 chord across ~4 octaves
    "Ciani", SCALE_CHROMATIC, 0, 16, 2, 1024, STYLE_EXTNORMAL,
    { 7, 19, 9, 31, 43, 48, 33, 52, 45, 4, 12, 19, 9, 33, 48, 28 },
    0x0001, 0x8000 },

  { // 1 - Bach: bright major, fast even sixteenths, arpeggio + scale
    "Bach", SCALE_MAJOR, 0, 16, 2, 600, 0,
    { 0, 4, 7, 12, 16, 12, 7, 4, 0, 2, 4, 5, 7, 9, 11, 12 },
    0x1111, 0x0000 },

  { // 2 - Chopin: A minor, slow and legato, wide romantic arpeggio
    "Chopin", SCALE_NATURAL_MINOR, 9, 16, 3, 200, STYLE_SLOPED,
    { 0, 3, 7, 10, 12, 15, 19, 15, 12, 10, 7, 3, 0, 3, 7, 10 },
    0x0101, 0x0000 },

  { // 3 - Aphex Twin: phrygian, fast, odd 13-step loop, gliding and jagged
    "Aphex Twin", SCALE_PHRYGIAN, 2, 13, 2, 200, STYLE_SLOPED,
    { 0, 1, 12, 3, 13, 5, 7, 1, 8, 15, 2, 11, 6, 0, 0, 0 },
    0x0889, 0x1000 },

  { // 4 - Kraftwerk: minimal minor motif, steady, robotic, short loop
    "Kraftwerk", SCALE_NATURAL_MINOR, 0, 8, 2, 2200, 0,
    { 0, 0, 7, 7, 3, 3, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0 },
    0x00FF, 0x0000 },

  { // 5 - Giorgio Moroder: driving 16th octave bassline (I Feel Love)
    "Moroder", SCALE_NATURAL_MINOR, 0, 16, 2, 700, 0,
    { 0, 12, 0, 12, 3, 12, 0, 12, 5, 12, 0, 12, 7, 12, 3, 12 },
    0xFFFF, 0x0000 },

  { // 6 - Vulfpeck: funky pentatonic-minor bass riff, syncopated
    "Vulfpeck", SCALE_PENTATONIC_MINOR, 0, 16, 2, 1400, 0,
    { 0, 0, 3, 0, 5, 0, 3, 5, 7, 0, 3, 0, 5, 3, 0, 10 },
    0x9249, 0x0000 },

  { // 7 - Philip Glass: bright major arpeggio, additive 12-step cell
    "Glass", SCALE_MAJOR, 0, 12, 2, 500, 0,
    { 0, 4, 7, 11, 7, 4, 0, 4, 7, 11, 12, 7, 0, 0, 0, 0 },
    0x0FFF, 0x0000 },

  { // 8 - Tangerine Dream: Berlin-school minor 16th arpeggio, gated
    "Tangerine", SCALE_NATURAL_MINOR, 0, 16, 2, 800, STYLE_SLOPED,
    { 0, 3, 7, 10, 12, 10, 7, 3, 0, 7, 12, 15, 12, 7, 3, 0 },
    0xFFFF, 0x0000 },

  { // 9 - Vangelis: slow lush major pads, wide and legato
    "Vangelis", SCALE_MAJOR, 0, 16, 3, 400, STYLE_SLOPED,
    { 0, 7, 12, 16, 19, 16, 12, 7, 0, 4, 9, 12, 16, 12, 9, 4 },
    0x0101, 0x0000 },

  { // 10 - Wendy Carlos: Switched-On Bach, crisp major arpeggio
    "Carlos", SCALE_MAJOR, 0, 16, 2, 700, 0,
    { 0, 7, 4, 12, 7, 16, 12, 7, 4, 0, 7, 4, 12, 7, 4, 0 },
    0x1111, 0x0000 },

  { // 11 - Daft Punk: filtered minor octave groove, four-on-the-floor
    "Daft Punk", SCALE_NATURAL_MINOR, 0, 16, 2, 1000, 0,
    { 0, 0, 12, 0, 3, 0, 12, 0, 5, 0, 12, 0, 7, 3, 12, 0 },
    0xFFFF, 0x0000 },
};

const uint8_t STYLE_COUNT = sizeof(styles) / sizeof(styles[0]);
