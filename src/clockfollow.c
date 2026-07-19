#include "clockfollow.h"

// ---------------------------------------------------------------------------
// Knob -> ratio zones, anchored to the PANEL LEGEND (0.5 .. 1 .. 2 .. 4).
//
// The two board revisions use DIFFERENT pot tapers, so the panel marks land
// at different calibrated values and each hardware gets its own MEASURED
// anchors (four-point knob measurement over SWD, calibrated counts):
//
//   REV1 (audio-taper pot):  0.5=0,  "1"=~2013 (49%), "2"=~3579, CW=~4095
//   v2   (near-linear pot):  0.5=0,  "1"=~1517 (37%), "2"=~2842, CW=~4086
//
// The x1 zone is centred on the measured printed-"1"; divides fill the travel
// below it and multiplies above, in equal count-width zones. The printed
// legend is log-spaced and only reaches 4, so above "1" the marks are
// approximate: turn by ear/count - full CW is always x8.
// ---------------------------------------------------------------------------

#ifndef MARF_HW
#define MARF_HW 2
#endif

#define CF_SIDE_ZONES  7u
#if MARF_HW == 1
  // REV1: x1 = 1763..2263 (centred on measured 2013)
  #define CF_X1_LO       1763u
  #define CF_X1_HI       2263u
  #define CF_DIV_ZONE_W  252u    // 1763 / 7
  #define CF_MUL_ZONE_W  262u    // (4096 - 2264) / 7
#else
  // v2: x1 = 1267..1767 (centred on measured 1517)
  #define CF_X1_LO       1267u
  #define CF_X1_HI       1767u
  #define CF_DIV_ZONE_W  181u    // 1267 / 7
  #define CF_MUL_ZONE_W  333u    // (4096 - 1768) / 7
#endif
#define CF_HYST        40u     // extra counts to move before a zone change

// Zone bounds for a valid ratio.
static void zone_bounds(int8_t ratio, uint16_t *lo, uint16_t *hi) {
  if (ratio == 1) {
    *lo = CF_X1_LO; *hi = CF_X1_HI;
  } else if (ratio < 0) {
    // /8 is zone 0 (full CCW), /2 is zone 6
    uint16_t i = (uint16_t) (8 + ratio);           // -8 -> 0 .. -2 -> 6
    *lo = (uint16_t) (i * CF_DIV_ZONE_W);
    *hi = (uint16_t) ((i == CF_SIDE_ZONES - 1) ? (CF_X1_LO - 1)
                                               : ((i + 1) * CF_DIV_ZONE_W - 1));
  } else {
    // x2 is zone 0 (just past the printed "1"), x8 is zone 6 (full CW)
    uint16_t i = (uint16_t) (ratio - 2);           // 2 -> 0 .. 8 -> 6
    *lo = (uint16_t) (CF_X1_HI + 1 + i * CF_MUL_ZONE_W);
    *hi = (uint16_t) ((i == CF_SIDE_ZONES - 1) ? 4095
                                               : (CF_X1_HI + (i + 1) * CF_MUL_ZONE_W));
  }
}

// Raw (no hysteresis) zone lookup.
static int8_t ratio_raw(uint16_t knob) {
  if (knob >= CF_X1_LO && knob <= CF_X1_HI) return 1;
  if (knob < CF_X1_LO) {
    uint16_t i = (uint16_t) (knob / CF_DIV_ZONE_W);
    if (i > CF_SIDE_ZONES - 1) i = CF_SIDE_ZONES - 1;
    return (int8_t) (i - 8);                       // 0 -> -8 .. 6 -> -2
  }
  uint16_t i = (uint16_t) ((knob - (CF_X1_HI + 1)) / CF_MUL_ZONE_W);
  if (i > CF_SIDE_ZONES - 1) i = CF_SIDE_ZONES - 1;
  return (int8_t) (i + 2);                         // 0 -> x2 .. 6 -> x8
}

static uint8_t ratio_valid(int8_t r) {
  return (r == 1) || (r >= 2 && r <= 8) || (r >= -8 && r <= -2);
}

int8_t cf_ratio_from_knob(uint16_t knob, int8_t current) {
  if (knob > 4095) knob = 4095;
  if (ratio_valid(current)) {
    // Stay in the current zone until the knob clearly leaves it.
    uint16_t lo, hi;
    zone_bounds(current, &lo, &hi);
    uint16_t lo_h = (lo > CF_HYST) ? (uint16_t) (lo - CF_HYST) : 0;
    uint16_t hi_h = (uint16_t) ((hi + CF_HYST > 4095) ? 4095 : hi + CF_HYST);
    if (knob >= lo_h && knob <= hi_h) return current;
  }
  return ratio_raw(knob);
}

// ---------------------------------------------------------------------------
// Per-occurrence humanize offset.
// ---------------------------------------------------------------------------

#define CF_HUMANIZE_DEAD 200   // bottom counts that read as "on the clock"

uint32_t cf_humanize_depth(uint16_t tlevel, uint32_t base_width) {
  if (tlevel > 4095) tlevel = 4095;
  if (tlevel <= CF_HUMANIZE_DEAD) return 0;    // slider down: dead on the grid
  // Linear depth: half slider = half depth, up to base/2 at the top (a full
  // slider can throw a step half-way to its neighbours; the width/gate clamps
  // keep order and retriggering intact).
  uint32_t x = ((uint32_t) (tlevel - CF_HUMANIZE_DEAD) * 4096u)
               / (4095u - CF_HUMANIZE_DEAD);            // 0..4096
  return (uint32_t) (((uint64_t) x * (base_width / 2u)) >> 12);
}

int32_t cf_humanize_unit(uint32_t rnd) {
  return (int32_t) (rnd % 8191u) - 4095;   // uniform -4095..+4095
}

// ---------------------------------------------------------------------------
// Period tracking.
// ---------------------------------------------------------------------------

uint32_t cf_update_period(uint32_t period, uint32_t delta) {
  if (period == 0) return delta;
  uint32_t diff = (delta > period) ? (delta - period) : (period - delta);
  if (diff <= period / 8u) {
    // Jitter: light smoothing
    return (period * 3u + delta) / 4u;
  }
  // Tempo change: snap
  return delta;
}

// ---------------------------------------------------------------------------
// Step widths.
// ---------------------------------------------------------------------------

uint32_t cf_base_width(uint32_t period, int8_t ratio) {
  uint32_t base;
  if (ratio < 0) {
    base = period * (uint32_t) (-ratio);
  } else if (ratio > 1) {
    base = period / (uint32_t) ratio;
  } else {
    base = period;
  }
  if (base < CF_MIN_WIDTH_TICKS) base = CF_MIN_WIDTH_TICKS;
  return base;
}

uint32_t cf_step_width(uint32_t period, int8_t ratio,
                       int32_t off_cur, int32_t off_next) {
  uint32_t base = cf_base_width(period, ratio);
  int64_t w = (int64_t) base + off_next - off_cur;
  int64_t min_w = base / 5u;
  if (min_w < CF_MIN_WIDTH_TICKS) min_w = CF_MIN_WIDTH_TICKS;
  if (w < min_w) w = min_w;
  return (uint32_t) w;
}
