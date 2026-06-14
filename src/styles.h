#ifndef __STYLES_H
#define __STYLES_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// "Style" presets. Each style is a complete sequence feel: a scale + root, a
// tempo, a per-stage pitch pattern (scale-relative semitones), a rhythm (which
// stages pulse), and feel flags (legato slope, external-input normalling).
// LoadStyle() (in controller.c) programs the sequencer from one of these, and
// the Stop+Sustain+First+Last chord cycles through them.
//
// Styles are named after artists/composers and are evocations of their feel,
// not transcriptions. The roster grows toward a larger library over time.
// ---------------------------------------------------------------------------

#define STYLE_SLOPED     0x01   // legato (sloped) stages
#define STYLE_EXTNORMAL  0x02   // follow external input A when a CV is present

typedef struct {
  const char *name;
  uint8_t  scale;        // ScaleId
  uint8_t  root;         // 0..11
  uint8_t  length;       // loop length 2..16
  uint8_t  time_range;   // 1 (fastest) .. 4 (slowest)
  uint16_t time_level;   // 0..4095 within the range (tempo)
  uint8_t  flags;        // STYLE_* bits
  int8_t   degree[16];   // per-stage pitch, semitones relative to root
  uint16_t pulse1;       // bitmask: stages that fire pulse 1
  uint16_t pulse2;       // bitmask: stages that fire pulse 2
} Style;

extern const Style styles[];
extern const uint8_t STYLE_COUNT;

#endif
