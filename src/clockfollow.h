#ifndef __CLOCKFOLLOW_H
#define __CLOCKFOLLOW_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// External clock follow ("clock sync") - pure helpers.
//
// When a channel is externally clocked (a clock patched into BOTH Start and
// Stop, i.e. a stream of advance pulses), the AFG locks to the incoming clock:
//
//   * The Time Multiplier knob selects an integer clock ratio instead of a
//     continuous multiplier. Noon (11 to 1 o'clock) = x1 (one step per clock);
//     clockwise multiplies the clock up to x8, counter-clockwise divides it
//     down to /8. All integer ratios are available:
//         /8 /7 /6 /5 /4 /3 /2  [x1]  x2 x3 x4 x5 x6 x7 x8
//
//   * The time sliders (which set nothing time-wise while clocked) become a
//     per-step "humanize" amount: slider down = right on the clock; raising it
//     introduces a random timing shift, freshly drawn each time the step
//     plays, early or late, scaling linearly up to half a step at the top.
//     The long-term grid stays locked to the incoming clock.
//
// These helpers are pure and host-tested; the AFG integration lives in afg.c.
// Ratio encoding: negative = divide (-8..-2), +1 = unity, +2..+8 = multiply.
// ---------------------------------------------------------------------------

// All periods/widths below are in AFG ticks (32 kHz).

// Clock qualification: pulses spaced outside these bounds don't lock.
#define CF_MIN_PERIOD_TICKS  640u      // 20 ms   (50 Hz max clock)
#define CF_MAX_PERIOD_TICKS  64000u    // 2 s     (matches the drop-out timeout)
#define CF_TIMEOUT_MS        2000u     // no pulse for this long -> free-run

// Never let a (sub)step get shorter than this (2 ms), whatever the ratio/nudge.
#define CF_MIN_WIDTH_TICKS   64u

// Map the Time Multiplier knob (calibrated 0..4095) to a clock ratio, with
// hysteresis: `current` is the ratio in effect and is kept until the knob
// moves clearly outside its zone (so zone-edge jitter can't flap the ratio).
// Pass 0 for `current` when no ratio is in effect yet.
int8_t cf_ratio_from_knob(uint16_t knob, int8_t current);

// Humanize DEPTH in ticks from a step's time slider (0..4095): bottom (with a
// dead zone) = 0 (dead on the grid), rising linearly to base_width/2 at the
// top.
uint32_t cf_humanize_depth(uint16_t tlevel, uint32_t base_width);

// Per-occurrence random unit (-4095..+4095) from a fresh random word. The
// caller latches ONE unit per step occurrence and scales it by the LIVE depth
// with cf_humanize_offset(), so the slider responds immediately while the
// random draw stays fixed for that occurrence.
int32_t cf_humanize_unit(uint32_t rnd);

static inline int32_t cf_humanize_offset(int32_t unit, uint32_t depth) {
  return (int32_t) (((int64_t) unit * (int32_t) depth) / 4095);
}

// Fold a newly measured pulse-to-pulse delta into the running period estimate:
// small changes (jitter) are smoothed, larger ones (a tempo change) snap.
uint32_t cf_update_period(uint32_t period, uint32_t delta);

// Base (un-nudged) width of one step at this ratio: period * N when dividing,
// period / N when multiplying. Clamped to CF_MIN_WIDTH_TICKS.
uint32_t cf_base_width(uint32_t period, int8_t ratio);

// Effective width of the current step from the two LATCHED offsets: its start
// was shifted by off_cur, its end is shifted by off_next, so
// width = base + off_next - off_cur, clamped so a step can never collapse
// (>= base/5 and >= CF_MIN_WIDTH_TICKS).
uint32_t cf_step_width(uint32_t period, int8_t ratio,
                       int32_t off_cur, int32_t off_next);

#endif
