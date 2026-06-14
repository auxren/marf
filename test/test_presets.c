/*
 * Host unit tests for the factory preset bank (presets.c).
 */
#include <stdio.h>
#include <string.h>

#include "presets.h"
#include "program.h"
#include "scales.h"
#include "storage.h"
#include "eprom.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_presets_tests(void);

/* Fake EEPROM backing store + layout from test_support.c / set up here. */
extern unsigned char fake_eeprom[0x10000];

static int popcount_time_range(const uStep *s) {
  return s->b.TimeRange_p03 + s->b.TimeRange_p3 + s->b.TimeRange_3 + s->b.TimeRange_30;
}
static int popcount_voltage_range(const uStep *s) {
  return s->b.FullRange + s->b.Voltage0 + s->b.Voltage2 +
         s->b.Voltage4 + s->b.Voltage6 + s->b.Voltage8;
}

static void test_build_each_preset_valid(void) {
  printf("test_build_each_preset_valid\n");
  for (uint8_t slot = 0; slot < 16; slot++) {
    ProgramPayload p;
    BuildFactoryPreset(&p, slot);

    /* per-AFG scale/root are in range */
    CHECK(p.scale[0] < SCALE_COUNT && p.scale[1] < SCALE_COUNT);
    CHECK(p.root[0] < 12 && p.root[1] < 12);
    CHECK(p.scale[0] == p.scale[1] && p.root[0] == p.root[1]);

    /* two-part: AFG 1 on stages 1-16, AFG 2 saved onto stages 17-32 */
    CHECK(p.section[0] == 0 && p.section[1] == 1);

    for (uint8_t i = 0; i < 32; i++) {
      const uStep *s = &p.steps[i];
      /* exactly one voltage range and one time range bit */
      CHECK(popcount_voltage_range(s) == 1);
      CHECK(popcount_time_range(s) == 1);
      /* internal sources so nothing has to be patched */
      CHECK(s->b.VoltageSource == 0 && s->b.TimeSource == 0);
      /* presets are quantized to their scale */
      CHECK(s->b.Quantize == 1);
      /* op-modes off so it just plays */
      CHECK(s->b.OpModeSTOP == 0 && s->b.OpModeSUSTAIN == 0 && s->b.OpModeENABLE == 0);
      /* slider values in DAC range */
      CHECK(p.sliders[i].VLevel <= 4095);
      CHECK(p.sliders[i].TLevel <= 4095);
    }

    /* Each 16-stage block (AFG 1 = 0-15, AFG 2 = 16-31) loops independently:
     * First on its own first stage, exactly one First and one Last per block. */
    for (uint8_t base = 0; base <= 16; base += 16) {
      int first_count = 0, last_count = 0;
      for (uint8_t off = 0; off < 16; off++) {
        first_count += p.steps[base + off].b.CycleFirst;
        last_count  += p.steps[base + off].b.CycleLast;
      }
      CHECK(p.steps[base].b.CycleFirst == 1);
      CHECK(first_count == 1);
      CHECK(last_count == 1);
    }
  }
}

static void test_themes(void) {
  printf("test_themes\n");
  ProgramPayload p;

  /* Slot 8 = "On the Run": fast (sub-second range), stepped, both pulses. */
  BuildFactoryPreset(&p, 8);
  CHECK(p.steps[0].b.Sloped == 0);
  CHECK(p.steps[0].b.TimeRange_p3 == 1 || p.steps[0].b.TimeRange_p03 == 1);
  CHECK(p.steps[0].b.OutputPulse1 == 1 && p.steps[0].b.OutputPulse2 == 1);

  /* Slot 6 = polymeter: AFG 1 and AFG 2 have DIFFERENT loop lengths (7 vs 5),
   * so their Last markers land at different offsets within each block. */
  BuildFactoryPreset(&p, 6);
  int last_a = -1, last_b = -1;
  for (uint8_t off = 0; off < 16; off++) {
    if (p.steps[off].b.CycleLast)        last_a = off;
    if (p.steps[16 + off].b.CycleLast)   last_b = off;
  }
  CHECK(last_a == 6);   /* 7-step loop */
  CHECK(last_b == 4);   /* 5-step loop */

  /* Slot 5 = Reich phasing: identical cell on both AFGs (same length + pitches). */
  BuildFactoryPreset(&p, 5);
  for (uint8_t off = 0; off < 16; off++) {
    CHECK(p.sliders[off].VLevel == p.sliders[16 + off].VLevel);
    CHECK(p.steps[off].b.CycleLast == p.steps[16 + off].b.CycleLast);
  }

  /* Slot 15 = harmony: same length both blocks, but AFG 2 plays different (higher)
   * pitches than AFG 1. */
  BuildFactoryPreset(&p, 15);
  int any_higher = 0, all_lengths_match = 1;
  int la = -1, lb = -1;
  for (uint8_t off = 0; off < 16; off++) {
    if (p.steps[off].b.CycleLast)      la = off;
    if (p.steps[16 + off].b.CycleLast) lb = off;
    if (p.sliders[16 + off].VLevel > p.sliders[off].VLevel) any_higher = 1;
  }
  if (la != lb) all_lengths_match = 0;
  CHECK(all_lengths_match == 1);
  CHECK(any_higher == 1);
}

/* Lay out 16 program slots at the head and the factory record at the tail,
 * mirroring EpromInitializeMemoryLayout (not compiled for host tests). */
static void setup_eprom_layout(void) {
  uint16_t start = 0, size = sizeof(StoredProgram);
  for (uint8_t i = 0; i < 16; i++) {
    eprom_memory.programs[i].start = start;
    eprom_memory.programs[i].size  = size;
    start += size;
  }
  uint16_t tail = 0xFFFF;
  tail -= sizeof(StoredCal);
  tail -= sizeof(StoredTwoPointCal);
  tail -= sizeof(StoredFactory);
  eprom_memory.factory_data.start = tail;
  eprom_memory.factory_data.size  = sizeof(StoredFactory);
}

static void write_user_save(uint8_t slot, uint16_t marker) {
  StoredProgram user;
  memset(&user, 0, sizeof(user));
  user.payload.sliders[0].VLevel = marker;
  marf_stored_program_finalize(&user);
  CAT25512_write_block(eprom_memory.programs[slot].start,
                       (unsigned char *) &user, eprom_memory.programs[slot].size);
}

static uint16_t read_marker(uint8_t slot) {
  StoredProgram sp;
  CAT25512_read_block(eprom_memory.programs[slot].start,
                      (unsigned char *) &sp, eprom_memory.programs[slot].size);
  return sp.payload.sliders[0].VLevel;
}

static void test_populate_fills_empty_preserves_saved(void) {
  printf("test_populate_fills_empty_preserves_saved\n");
  setup_eprom_layout();

  /* Blank the whole chip (all slots + factory record invalid). */
  memset(fake_eeprom, 0xFF, 0x10000);

  /* User saves a program into slot 5 (the save path also marks it user-owned). */
  write_user_save(5, 2222);
  FactoryMarkUserSave(5);

  PopulateFactoryPresets();

  /* Every slot is now a valid program. */
  for (uint8_t i = 0; i < 16; i++) {
    StoredProgram sp;
    CAT25512_read_block(eprom_memory.programs[i].start,
                        (unsigned char *) &sp, eprom_memory.programs[i].size);
    CHECK(marf_stored_program_valid(&sp) == 1);
  }

  /* Slot 5 is still the user's save, untouched; a factory slot matches its build. */
  CHECK(read_marker(5) == 2222);
  StoredProgram check0;
  CAT25512_read_block(eprom_memory.programs[0].start,
                      (unsigned char *) &check0, eprom_memory.programs[0].size);
  ProgramPayload expect0;
  BuildFactoryPreset(&expect0, 0);
  CHECK(memcmp(&check0.payload, &expect0, sizeof(expect0)) == 0);

  /* Idempotent: the bank is now current, so a second run rewrites nothing. */
  PopulateFactoryPresets();
  CHECK(read_marker(5) == 2222);
}

static void test_bank_update_refreshes_owned_only(void) {
  printf("test_bank_update_refreshes_owned_only\n");
  setup_eprom_layout();
  memset(fake_eeprom, 0xFF, 0x10000);

  /* Simulate an older bank already on the chip: slot 0 is a factory-owned slot
   * holding stale content; slot 1 is a user save (not owned). */
  write_user_save(0, 9999);   /* stale "old factory" content in an owned slot */
  write_user_save(1, 4444);   /* user's program */

  StoredFactory fs;
  memset(&fs, 0, sizeof(fs));
  fs.payload.bank_version = 0;            /* older than MARF_FACTORY_BANK_VER */
  fs.payload.owned_mask   = 0x0001u;      /* only slot 0 is factory-owned */
  marf_stored_factory_finalize(&fs);
  CAT25512_write_block(eprom_memory.factory_data.start,
                       (unsigned char *) &fs, eprom_memory.factory_data.size);

  PopulateFactoryPresets();

  /* Owned + outdated slot 0 was refreshed to the current factory preset. */
  StoredProgram s0;
  CAT25512_read_block(eprom_memory.programs[0].start,
                      (unsigned char *) &s0, eprom_memory.programs[0].size);
  ProgramPayload expect0;
  BuildFactoryPreset(&expect0, 0);
  CHECK(memcmp(&s0.payload, &expect0, sizeof(expect0)) == 0);
  CHECK(read_marker(0) != 9999);

  /* The user's slot 1 (not owned) was left exactly as it was. */
  CHECK(read_marker(1) == 4444);

  /* Bank version is now recorded as current. */
  CAT25512_read_block(eprom_memory.factory_data.start,
                      (unsigned char *) &fs, eprom_memory.factory_data.size);
  CHECK(marf_stored_factory_valid(&fs) == 1);
  CHECK(fs.payload.bank_version == MARF_FACTORY_BANK_VER);
}

static void test_feel(void) {
  printf("test_feel\n");
  int presets_with_rhythm = 0;   /* >1 distinct stage length in block A */
  int presets_with_slide  = 0;   /* any sloped stage */
  int bank_has_rest = 0;         /* any stage with no trigger (pulse off) */
  for (uint8_t slot = 0; slot < 16; slot++) {
    ProgramPayload p;
    BuildFactoryPreset(&p, slot);

    /* No preset is fully sloped (that's the "too much slide" failure). */
    int sloped = 0;
    for (uint8_t i = 0; i < 16; i++) sloped += p.steps[i].b.Sloped;
    CHECK(sloped < 16);
    if (sloped > 0) presets_with_slide++;

    /* Rhythm: does block A use more than one stage length? */
    uint16_t t0 = p.sliders[0].TLevel;
    for (uint8_t i = 1; i < 16; i++)
      if (p.sliders[i].TLevel != t0) { presets_with_rhythm++; break; }

    for (uint8_t i = 0; i < 16; i++)
      if (p.steps[i].b.OutputPulse1 == 0) bank_has_rest = 1;
  }
  /* Several presets have rhythmic note lengths (not one flat duration). */
  CHECK(presets_with_rhythm >= 4);
  /* Slides are a deliberate minority -- not the default everywhere. */
  CHECK(presets_with_slide >= 1 && presets_with_slide <= 6);
  /* Rests/accents create groove somewhere in the bank. */
  CHECK(bank_has_rest == 1);
}

static void test_two_part(void) {
  printf("test_two_part\n");
  int distinct = 0;   /* presets whose AFG 2 part differs from AFG 1 */
  for (uint8_t slot = 0; slot < 16; slot++) {
    ProgramPayload p;
    BuildFactoryPreset(&p, slot);
    int differs = 0;
    for (uint8_t off = 0; off < 16; off++) {
      if (p.sliders[off].VLevel != p.sliders[16 + off].VLevel ||
          p.sliders[off].TLevel != p.sliders[16 + off].TLevel ||
          p.steps[off].b.PulseWidth != p.steps[16 + off].b.PulseWidth) {
        differs = 1; break;
      }
    }
    if (differs) distinct++;
  }
  /* Most presets are real two-part arrangements (AFG 1 vs AFG 2); only a few
   * (e.g. Reich phasing) intentionally put the same cell on both. */
  CHECK(distinct >= 12);
}

void run_presets_tests(void) {
  test_build_each_preset_valid();
  test_themes();
  test_feel();
  test_two_part();
  test_populate_fills_empty_preserves_saved();
  test_bank_update_refreshes_owned_only();
}
