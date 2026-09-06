#ifndef __PRESETS_H
#define __PRESETS_H

#include <stdint.h>
#include "storage.h"   // ProgramPayload

// Build factory preset `slot` (0..15) into `p`. Pure / host-testable. Each
// preset is a two-part arrangement: block A = AFG 1 (stages 1-16) and block B =
// AFG 2 (stages 17-32) are written to complement each other (bass under arp, pad
// under riff, counter-melody, ...). Bank: a mix of recognizable synth
// lines (I Feel Love, Berlin School, Halloween, Stranger Things, Trans-Europe
// Express, Cars, Blue Monday, On the Run in slot 9, Bach Prelude, Blade Runner,
// Glass) and hardware showcases (Reich phasing, 7-vs-5 polymeter, acid slides,
// whole-tone drift, two-voice harmony).
void BuildFactoryPreset(ProgramPayload *p, uint8_t slot);

// Seed the factory presets into any EEPROM program slot that is empty/invalid,
// within the factory region only (slots 0 .. MARF_FACTORY_SLOTS-1); the
// user-only slots above it are never written here.
// and refresh any factory-owned slot when the bank content has changed
// (MARF_FACTORY_BANK_VER bumped). Slots the user has saved to are left
// untouched. Call once at boot, after loading calibration.
void PopulateFactoryPresets(void);

// Mark `slot` as user-owned after a manual save, so it is never overwritten by
// a future factory bank update. Slots outside the factory region (0 ..
// MARF_FACTORY_SLOTS-1) have no ownership bit and are ignored here.
void FactoryMarkUserSave(uint8_t slot);

#endif
