#include "presets.h"

#include <stddef.h>
#include <string.h>

#include "program.h"   // uStep, StepSliders
#include "scales.h"    // ScaleId
#include "storage.h"   // StoredProgram, marf_stored_program_*
#include "eprom.h"     // eprom_memory, MemoryRange
#include "CAT25512.h"  // CAT25512_read_block / write_block

// ---------------------------------------------------------------------------
// Factory preset bank.
//
// Sixteen ready-to-play sequences are pre-loaded into any program slot that a
// user has never saved to (or that calibration has erased). They are a curated
// starting point -- audition and tweak freely; saving over a slot keeps your
// own version (PopulateFactoryPresets never overwrites a valid saved slot).
//
// Each stage is described per-step: pitch, a relative *length* (so sequences
// have rhythm, not one flat duration) and an *articulation* (stepped, slid,
// stabbed/accented, held, or rested). That is what gives each preset its feel
// -- slides are used sparingly (only where they belong, e.g. an acid line),
// note lengths vary, and accents/rests create groove.
//
// Each preset is a TWO-PART arrangement: block A is AFG 1 (stages 1-16), block B
// is AFG 2 (stages 17-32), written to complement each other -- bass under arp,
// pad under riff, counter-melody, etc. Run both AFGs (AFG 2 shifted to stages
// 17-32) to hear the whole thing; AFG 1 alone is still a complete sequence. Only
// Reich phasing puts the same cell on both blocks.
//
// Pitches are chromatic semitone offsets from the 0 V reference; every stage is
// Quantized to the preset's scale, so a slider nudge still lands in key.
// (Transcriptions are evocations, not exact scores -- a place to start.)
// ---------------------------------------------------------------------------

// Pulse/gate widths (0..15 -> ~1%..99% of the step).
#define GATE_LEGATO  14   // ~92% -- sustained / held
#define GATE_MED      7   // ~47% -- ordinary note
#define GATE_STAB     2   // ~14% -- short, accented stab

// Per-stage articulation.
enum { ART_STEP = 0, ART_SLIDE, ART_STAB, ART_HOLD, ART_REST };
#define PR (-100)         // rest sentinel (no trigger; holds the previous pitch)

typedef struct {
  int8_t  note;           // semitone offset, or PR for a rest
  uint8_t len;            // duration in units (see PresetDef.unit_ms)
  uint8_t art;            // ART_*
} PStep;

typedef struct {
  uint8_t        scale;       // ScaleId for both sequences
  uint8_t        root;        // 0..11
  uint8_t        time_range;  // base time range (see set_time_range)
  uint16_t       unit_ms;     // milliseconds per length unit (tempo)
  const PStep   *a;  uint8_t na;   // block A (AFG 1) steps / length
  const PStep   *b;  uint8_t nb;   // block B (AFG 2); NULL => mirror A
} PresetDef;

// Time-range selector values (kept symbolic; mapped to the uStep bit below).
enum { TR_P03 = 0, TR_P3 = 1, TR_3 = 2, TR_30 = 3 };

// Compact step constructors keep the tables readable.
#define ST(n, l) { (n), (l), ART_STEP }    // stepped, medium gate
#define SL(n, l) { (n), (l), ART_SLIDE }   // slide (glide) into this note
#define SB(n, l) { (n), (l), ART_STAB }    // short accented stab (both pulses)
#define HD(n, l) { (n), (l), ART_HOLD }    // long sustained note
#define RS(l)    { PR,  (l), ART_REST }    // rest (no trigger)
#define ARRN(x)  (uint8_t)(sizeof(x) / sizeof((x)[0]))

// ---- Iconic synth lines ---------------------------------------------------
// "I Feel Love": relentless staccato octave-bouncing 16th-note bass.
static const PStep i_feel_love[] = {
  SB(0,1),SB(12,1),SB(0,1),SB(12,1), SB(0,1),SB(12,1),SB(0,1),SB(12,1),
  SB(10,1),SB(22,1),SB(10,1),SB(22,1), SB(7,1),SB(19,1),SB(7,1),SB(19,1) };
// Tangerine Dream / Berlin School: driving minor arp, accents on the beat.
static const PStep berlin[] = {
  HD(0,2),SB(3,1),SB(7,1),SB(10,1), ST(12,2),SB(10,1),SB(7,1),SB(3,1),
  HD(0,2),SB(3,1),SB(7,1),SB(12,1), ST(15,2),SB(10,1),SB(7,1),SB(3,1) };
// John Carpenter "Halloween": 5/4 ostinato (odd loop) with a 3+2 limp.
static const PStep halloween[] = { SB(0,2),SB(7,1),SB(0,1),SB(7,1),SB(3,1) };
// Stranger Things: bright ascending arpeggio, notes left ringing.
static const PStep stranger[] = {
  HD(0,2),HD(4,2),HD(7,2),HD(11,2), HD(12,2),HD(16,2),HD(19,2),HD(23,2) };
// Bach "Prelude in C": flowing even 16ths (Switched-On nod).
static const PStep bach_prelude[] = {
  ST(0,1),ST(4,1),ST(7,1),ST(12,1), ST(16,1),ST(7,1),ST(12,1),ST(16,1) };
// Steve Reich phasing: even cell on BOTH AFGs; nudge a Time Multiply to phase.
static const PStep reich[] = {
  ST(0,1),ST(2,1),ST(7,1),ST(9,1), ST(11,1),ST(2,1),ST(0,1),ST(9,1),
  ST(7,1),ST(2,1),ST(11,1),ST(9,1) };
// Polymeter: AFG 1 (7 steps) against AFG 2 (5 steps) -> never re-aligns.
static const PStep poly7[] = { ST(0,1),ST(3,1),ST(5,1),ST(7,1),ST(10,1),ST(12,1),ST(7,1) };
static const PStep poly5[] = { ST(0,1),ST(3,1),ST(5,1),ST(7,1),ST(10,1) };
// Acid / 303: staccato bass with SELECTIVE slides, accents and rests.
static const PStep acid[] = {
  SB(0,1),RS(1),SL(12,1),SB(0,1), SB(3,1),RS(1),SL(7,1),SB(0,1),
  SB(0,1),SL(10,1),RS(1),SB(3,1), SL(5,1),SB(0,1),SB(7,1),RS(1) };
// "On the Run" (Pink Floyd): the real EMS Synthi sequence (E G A G | D C D E)
// at ~165 BPM 16ths; stepped, gated, fast. Semitones from E.
static const PStep ontherun[] = {
  SB(0,1),SB(3,1),SB(5,1),SB(3,1), SB(10,1),SB(8,1),SB(10,1),SB(12,1) };
// Kraftwerk "Trans-Europe Express": motoric, even 8ths, last note held.
static const PStep tee[] = {
  SB(0,2),SB(3,2),SB(0,2),SB(3,2), SB(5,2),ST(3,2),SB(2,2),HD(0,2) };
// Gary Numan "Cars": syncopated minor riff, mixed lengths.
static const PStep cars[] = {
  ST(0,2),HD(7,2),SB(5,1),SB(3,1), ST(5,2),HD(7,2),HD(10,2),HD(7,2) };
// New Order "Blue Monday": fast driving 16th bass with rests/syncopation.
static const PStep blue_monday[] = {
  SB(0,1),SB(0,1),SB(0,1),SB(12,1), RS(1),SB(0,1),SB(10,1),RS(1),
  SB(7,1),SB(7,1),SB(7,1),SB(5,1), SB(3,1),SB(3,1),SB(0,1),RS(1) };

// ---- Melodic / quantizer showcase -----------------------------------------
// Vangelis "Blade Runner": lush slow pad -- slides belong here (portamento).
static const PStep blade_runner[] = {
  SL(19,4),SL(16,4),HD(14,4),SL(12,4), SL(9,4),HD(7,4),SL(4,4),HD(0,4) };
// Philip Glass-style minimalism: hypnotic even arpeggio that subtly shifts.
static const PStep glass[] = {
  HD(0,1),HD(7,1),HD(15,1),HD(0,1), HD(7,1),HD(15,1),HD(0,1),HD(8,1),
  HD(15,1),HD(0,1),HD(8,1),HD(17,1) };

// ---- Hardware showcases ----------------------------------------------------
// Generative drift: long odd whole-tone loop, mostly held with a few slides.
static const PStep drift[] = {
  SL(0,3),ST(2,2),HD(4,3),ST(6,2), SL(8,3),ST(10,2),HD(8,3),ST(6,2),
  SL(4,3),ST(2,2),HD(0,3),ST(2,2), HD(4,4) };
// Two-voice harmony: AFG 1 bass + AFG 2 a voice above, same length.
static const PStep harmony_a[] = {
  HD(0,2),HD(7,2),HD(3,2),HD(10,2), HD(0,2),HD(7,2),HD(5,2),HD(10,2) };
static const PStep harmony_b[] = {
  HD(7,2),HD(14,2),HD(10,2),HD(17,2), HD(7,2),HD(14,2),HD(12,2),HD(17,2) };

// ---- AFG 2 counter-parts (stages 17-32) -----------------------------------
// Run AFG 1 on its block (stages 1-16) and AFG 2 on these (stages 17-32) for a
// two-part arrangement. Loop lengths are matched to AFG 1 where the parts should
// lock, and deliberately different where they should evolve.
// I Feel Love: high held topline outlining the bass's chord motion (locks: 16).
static const PStep ifl_b[] = {
  HD(24,2),HD(19,2),HD(24,2),HD(19,2), HD(22,2),HD(17,2),HD(19,2),HD(12,2) };
// Berlin: a slower pad under the driving arp (locks: 16 vs 8).
static const PStep berlin_b[] = {
  HD(0,2),HD(7,2),HD(3,2),HD(10,2), HD(0,2),HD(7,2),HD(2,2),HD(7,2) };
// Halloween: a higher counter-line, also in 5 (locks with the 5/4 ostinato).
static const PStep halloween_b[] = { HD(12,2),SB(7,1),SB(8,1),SB(7,1),SB(3,1) };
// Stranger Things: a slow root/fifth pedal bass under the arpeggio (locks: 16).
static const PStep stranger_b[] = { HD(0,4),HD(7,4),HD(0,4),HD(5,4) };
// Bach: the left-hand pedal under the right-hand arpeggio.
static const PStep bach_b[] = { HD(0,4),HD(0,4) };
// Acid: a sparse high acid counter on the offbeats (locks: 16).
static const PStep acid_b[] = {
  RS(1),SB(12,1),RS(1),SB(15,1), RS(1),SB(12,1),RS(1),SB(19,1),
  RS(1),SB(15,1),RS(1),SB(12,1), RS(1),SB(17,1),RS(1),SB(12,1) };
// On the Run: a low pulsing "footsteps" pedal under the run (locks: 8 units).
static const PStep ontherun_b[] = { HD(0,3),RS(1),HD(0,3),RS(1) };
// Trans-Europe Express: the string melody riding over the motoric bass (locks).
static const PStep tee_b[] = {
  HD(12,2),HD(15,2),HD(14,2),HD(12,2), HD(10,2),HD(12,2),HD(7,2),HD(0,2) };
// Cars: a sustained chord pad under the riff (locks: 14 units).
static const PStep cars_b[] = { HD(0,3),HD(7,4),HD(3,3),HD(7,4) };
// Blue Monday: a higher syncopated stab line answering the bass (locks: 16).
static const PStep blue_monday_b[] = {
  RS(1),SB(12,1),RS(1),SB(15,1), SB(12,1),RS(1),SB(10,1),SB(12,1),
  RS(1),SB(19,1),RS(1),SB(17,1), SB(15,1),RS(1),SB(12,1),RS(1) };
// Blade Runner: a higher slow CS-80-style lead over the pad (locks: 8).
static const PStep blade_b[] = {
  SL(24,4),HD(21,4),SL(19,4),HD(16,4), SL(14,4),HD(12,4),SL(11,4),HD(7,4) };
// Glass: a shorter interlocking cell (8 vs 12) -- additive phasing texture.
static const PStep glass_b[] = {
  HD(0,1),HD(3,1),HD(10,1),HD(3,1), HD(0,1),HD(7,1),HD(10,1),HD(7,1) };
// Drift: a different-length whole-tone line (8 vs 13) for evolving texture.
static const PStep drift_b[] = {
  SL(12,3),HD(10,3),SL(8,3),HD(6,3), SL(4,3),HD(2,3),SL(0,3),HD(2,3) };

static const PresetDef PRESETS[16] = {
  // # 1  I Feel Love -- AFG1 octave bass, AFG2 high held topline
  { SCALE_PENTATONIC_MINOR, 0, TR_P3, 115, i_feel_love, ARRN(i_feel_love), ifl_b,    ARRN(ifl_b) },
  // # 2  Berlin School -- AFG1 driving arp, AFG2 slower pad
  { SCALE_NATURAL_MINOR,    0, TR_P3, 120, berlin,      ARRN(berlin),      berlin_b, ARRN(berlin_b) },
  // # 3  Halloween -- AFG1 5/4 ostinato, AFG2 counter-line (also in 5)
  { SCALE_NATURAL_MINOR,    6, TR_P3, 130, halloween,   ARRN(halloween),   halloween_b, ARRN(halloween_b) },
  // # 4  Stranger Things -- AFG1 ascending arp, AFG2 pedal bass
  { SCALE_MAJOR,            0, TR_P3, 110, stranger,    ARRN(stranger),    stranger_b, ARRN(stranger_b) },
  // # 5  Bach Prelude in C -- AFG1 right-hand arp, AFG2 left-hand pedal
  { SCALE_MAJOR,            0, TR_P3, 140, bach_prelude,ARRN(bach_prelude),bach_b,   ARRN(bach_b) },
  // # 6  Reich phasing -- identical cell on both AFGs (nudge Time Multiply)
  { SCALE_MAJOR,            0, TR_P3, 120, reich,       ARRN(reich),       reich, ARRN(reich) },
  // # 7  Polymeter -- AFG 1 (7) vs AFG 2 (5)
  { SCALE_PENTATONIC_MINOR, 0, TR_P3, 125, poly7,       ARRN(poly7),       poly5, ARRN(poly5) },
  // # 8  Acid / 303 -- AFG1 acid bass, AFG2 sparse high counter
  { SCALE_PENTATONIC_MINOR, 0, TR_P3, 115, acid,        ARRN(acid),        acid_b,   ARRN(acid_b) },

  // # 9  On the Run -- AFG1 the run, AFG2 low "footsteps" pedal
  { SCALE_NATURAL_MINOR,    0, TR_P3,  91, ontherun,    ARRN(ontherun),    ontherun_b, ARRN(ontherun_b) },

  // #10  Trans-Europe Express -- AFG1 motoric bass, AFG2 string melody
  { SCALE_NATURAL_MINOR,    0, TR_P3, 125, tee,         ARRN(tee),         tee_b,    ARRN(tee_b) },
  // #11  Gary Numan "Cars" -- AFG1 riff, AFG2 sustained chord pad
  { SCALE_NATURAL_MINOR,    0, TR_P3, 130, cars,        ARRN(cars),        cars_b,   ARRN(cars_b) },
  // #12  New Order "Blue Monday" -- AFG1 driving bass, AFG2 stab answer
  { SCALE_PENTATONIC_MINOR, 0, TR_P3, 110, blue_monday, ARRN(blue_monday), blue_monday_b, ARRN(blue_monday_b) },
  // #13  Blade Runner (Vangelis) -- AFG1 pad, AFG2 higher slow lead
  { SCALE_LYDIAN,           0, TR_3,  350, blade_runner,ARRN(blade_runner),blade_b,  ARRN(blade_b) },
  // #14  Philip Glass -- AFG1 12-step cell, AFG2 interlocking 8-step cell
  { SCALE_NATURAL_MINOR,    0, TR_P3, 120, glass,       ARRN(glass),       glass_b,  ARRN(glass_b) },
  // #15  Generative drift -- AFG1 13-step, AFG2 8-step whole-tone (evolving)
  { SCALE_WHOLE_TONE,       0, TR_3,  220, drift,       ARRN(drift),       drift_b,  ARRN(drift_b) },
  // #16  Two-voice harmony -- AFG 1 bass + AFG 2 above
  { SCALE_MAJOR,            0, TR_3,  260, harmony_a,   ARRN(harmony_a),   harmony_b, ARRN(harmony_b) },
};

// Map a semitone offset to a 12-bit VLevel. The pitch encoding (FullRange +
// Quantize, see GetStepVoltage) is semitone = VLevel * 4095/100 / semitone, i.e.
// VLevel ~= semitone * 4095 / 100. Clamp into the 0..4095 DAC range.
static uint16_t vlevel_for_note(int8_t note) {
  int v = ((int) note * 4095) / 100;
  if (v < 0)    v = 0;
  if (v > 4095) v = 4095;
  return (uint16_t) v;
}

// Pick the time-slider TLevel that yields `secs` of stage time in `tr`.
// Mirrors GetStepWidth: width_s = (TLevel/4095*28 + 2) * range_mult.
static uint16_t tlevel_for_seconds(float secs, uint8_t tr) {
  float mult = (tr == TR_P03) ? 0.001f
             : (tr == TR_P3)  ? 0.01f
             : (tr == TR_3)   ? 0.1f
                              : 1.0f;
  float t = (((secs / mult) - 2.0f) / 28.0f) * 4095.0f;
  if (t < 0.0f)    t = 0.0f;
  if (t > 4095.0f) t = 4095.0f;
  return (uint16_t) (t + 0.5f);
}

static void set_time_range(uStep *s, uint8_t tr) {
  s->b.TimeRange_p03 = 0;
  s->b.TimeRange_p3  = 0;
  s->b.TimeRange_3   = 0;
  s->b.TimeRange_30  = 0;
  switch (tr) {
    case TR_P03: s->b.TimeRange_p03 = 1; break;
    case TR_P3:  s->b.TimeRange_p3  = 1; break;
    case TR_30:  s->b.TimeRange_30  = 1; break;
    case TR_3:
    default:     s->b.TimeRange_3   = 1; break;
  }
}

// Apply an articulation to a stage: slope, gate width and which pulses fire.
static void apply_articulation(uStep *s, uint8_t art) {
  s->b.Sloped = (art == ART_SLIDE) ? 1 : 0;
  switch (art) {
    case ART_SLIDE: s->b.PulseWidth = GATE_LEGATO; s->b.OutputPulse1 = 1; s->b.OutputPulse2 = 0; break;
    case ART_STAB:  s->b.PulseWidth = GATE_STAB;   s->b.OutputPulse1 = 1; s->b.OutputPulse2 = 1; break;
    case ART_HOLD:  s->b.PulseWidth = GATE_LEGATO; s->b.OutputPulse1 = 1; s->b.OutputPulse2 = 0; break;
    case ART_REST:  s->b.PulseWidth = GATE_STAB;   s->b.OutputPulse1 = 0; s->b.OutputPulse2 = 0; break;
    case ART_STEP:
    default:        s->b.PulseWidth = GATE_MED;    s->b.OutputPulse1 = 1; s->b.OutputPulse2 = 0; break;
  }
}

// Fill one 16-stage block (base 0 = AFG 1, 16 = AFG 2) from a per-step pattern.
static void build_block(ProgramPayload *p, const PresetDef *d,
                        uint8_t base, const PStep *steps, uint8_t n) {
  if (n < 1)  n = 1;
  if (n > 16) n = 16;
  uint16_t prev_vlevel = 0;

  for (uint8_t off = 0; off < 16; off++) {
    uint8_t i = base + off;
    const PStep *src = &steps[off % n];
    uint8_t len = src->len ? src->len : 1;
    uStep s = {{ 0, 0, 0, 0 }};

    s.b.FullRange     = 1;            // span the full voltage range
    s.b.Quantize      = 1;            // snap to the preset's scale
    s.b.VoltageSource = 0;            // internal (slider) -- nothing to patch
    s.b.TimeSource    = 0;            // internal time slider
#if MARF_HW != 1
    s.b.TuringLength  = 6;            // default 8-bit register (full value range)
#endif
    set_time_range(&s, d->time_range);
    apply_articulation(&s, src->art);

    // Pitch: a rest holds the previous pitch (and fires no pulse).
    uint16_t vlevel = (src->note == PR) ? prev_vlevel : vlevel_for_note(src->note);
    prev_vlevel = vlevel;

    // One loop over the first n stages of the block; First on the first stage,
    // Last on stage n-1 (so AFG 1 and AFG 2 each loop their own block).
    s.b.CycleFirst = (off == 0)     ? 1 : 0;
    s.b.CycleLast  = (off == n - 1) ? 1 : 0;

    p->steps[i]          = s;
    p->sliders[i].VLevel = vlevel;
    p->sliders[i].TLevel = tlevel_for_seconds((float) (len * d->unit_ms) / 1000.0f,
                                              d->time_range);
  }
}

void BuildFactoryPreset(ProgramPayload *p, uint8_t slot) {
  if (slot > 15) slot = 15;
  const PresetDef *d = &PRESETS[slot];

  const PStep *b = d->b ? d->b : d->a;
  uint8_t nb = d->b ? d->nb : d->na;

  memset(p, 0, sizeof(*p));
  build_block(p, d, 0,  d->a, d->na);   // AFG 1, stages 1-16
  build_block(p, d, 16, b,    nb);      // AFG 2, stages 17-32

  // Same scale/root for both sequences.
  p->scale[0] = d->scale; p->scale[1] = d->scale;
  p->root[0]  = d->root;  p->root[1]  = d->root;

  // Two-part: AFG 1 plays stages 1-16, AFG 2 plays its part on stages 17-32.
  // Saved with the program so loading auto-shifts AFG 2 (see the load path).
  p->section[0] = 0;
  p->section[1] = 1;
}

static void read_factory_state(StoredFactory *f) {
  CAT25512_read_block(eprom_memory.factory_data.start,
                      (unsigned char *) f, eprom_memory.factory_data.size);
  if (!marf_stored_factory_valid(f)) {
    // No bookkeeping yet. This is either a virgin chip or an upgrade from
    // firmware that auto-seeded factory presets without tracking ownership.
    // In both cases every currently-occupied slot is ours to (re)seed, so claim
    // them all at bank version 0 -- which is older than the current bank, so
    // they all get refreshed to the new content below. The moment the user
    // saves over a slot (FactoryMarkUserSave) its bit is cleared and it is
    // protected from every future bank update.
    f->payload.bank_version = 0;
    f->payload.owned_mask   = 0xFFFFu;
  }
}

static void write_factory_state(StoredFactory *f) {
  marf_stored_factory_finalize(f);
  CAT25512_write_block(eprom_memory.factory_data.start,
                       (unsigned char *) f, eprom_memory.factory_data.size);
}

void PopulateFactoryPresets(void) {
  StoredProgram sp;
  StoredFactory fs;
  read_factory_state(&fs);

  uint8_t bank_outdated = (fs.payload.bank_version < MARF_FACTORY_BANK_VER);

  for (uint8_t slot = 0; slot < 16; slot++) {
    CAT25512_read_block(eprom_memory.programs[slot].start,
                        (unsigned char *) &sp,
                        eprom_memory.programs[slot].size);
    uint8_t valid = marf_stored_program_valid(&sp);
    uint8_t owned = (fs.payload.owned_mask >> slot) & 1u;

    // Seed an empty/invalid slot, or refresh a factory-owned slot when the bank
    // content has changed. Never touch a slot the user saved to.
    if (!valid || (owned && bank_outdated)) {
      BuildFactoryPreset(&sp.payload, slot);
      marf_stored_program_finalize(&sp);
      CAT25512_write_block(eprom_memory.programs[slot].start,
                           (unsigned char *) &sp,
                           eprom_memory.programs[slot].size);
      fs.payload.owned_mask |= (1u << slot);   // now factory-owned
    }
  }

  fs.payload.bank_version = MARF_FACTORY_BANK_VER;
  write_factory_state(&fs);
}

// Called after the user saves a program into `slot`: that slot is now theirs, so
// clear its factory-owned bit and it will never be overwritten by a bank update.
void FactoryMarkUserSave(uint8_t slot) {
  if (slot > 15) return;
  StoredFactory fs;
  read_factory_state(&fs);
  if (fs.payload.owned_mask & (1u << slot)) {
    fs.payload.owned_mask &= ~(1u << slot);
    write_factory_state(&fs);
  }
}
