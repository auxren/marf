#ifndef __BUS200E_H
#define __BUS200E_H

#include <stdint.h>

#include "storage.h"   // StoredProgram: our card blob = the EEPROM records, verbatim

// ---------------------------------------------------------------------------
// 200e preset-bus engine (docs/DESIGN-200e-bus.md).
//
// BSP-free and host-testable: this layer never touches hardware. The transport
// (i2c_bb.c on target, the test suite on host) feeds it a stream of
// BUS200E_EV_* events; actions run through the Bus200eOps callbacks supplied
// at init. Passing NULL ops gives the RX-log-only behaviour required for the
// first enabled build: every decoded command lands in the debug ring and
// nothing else happens.
//
// Protocol source: github.com/studiohsoftware/2WIRELESS (see the design note).
// Commands arrive as I2C writes to the general-call address in one of two
// framings, both parsed here:
//   PRIMO:     cmd [args...]            e.g. 0x00 N = recall preset N
//   pre-PRIMO: nBytes mod 0x22 sub [args...] (nBytes counts the bytes after it)
// ---------------------------------------------------------------------------

// Event encoding fed from the transport. Low byte = data byte when no flag is
// set; flags mark transaction boundaries and transport trouble.
#define BUS200E_EV_START 0x0100u  // general-call transaction opened
#define BUS200E_EV_STOP  0x0200u  // transaction closed: parse the frame
#define BUS200E_EV_OVF   0x0400u  // transport lost events: poison this frame

// Our identity in command payloads (NOT the I2C wire address -- commands ride
// the general call; module addresses are payload bytes).
//
// Chosen to sit OUTSIDE the published 200e module address table entirely.
// That table (Studio H's getDisplayMessage() switch, mirrored in their
// GetPresets.html) runs from 0x10 to 0x72; storage cards occupy 0x50-0x5F;
// 0x22 is the preset manager's own source address; and 0x00-0x07 plus
// 0x78-0x7F are reserved by I2C itself. That leaves 0x08-0x0F unclaimed by
// anything, so a MARF cannot collide with a real module however the case is
// populated.
//
// The previous default was 0x3C, which the table lists as "281 C1" -- a latent
// collision for anyone running a 281e alongside a MARF.
//
// Still UNCONFIRMED against a live system's enumeration (a 225e shows a
// module's address on remote-enable hold); card backup/restore are addressed
// to it, so verify before trusting those.
#ifndef BUS200E_MODULE_ADDR
#define BUS200E_MODULE_ADDR 0x0E
#endif

// Our EEPROM program slots, which now cover the whole bus preset space: a
// preset manager can address all 30 the way it would any other 200e module.
#define BUS200E_SLOT_COUNT  MARF_PROGRAM_SLOTS
#define BUS200E_BUS_PRESETS 30   // the bus preset space is 0-29; anything at or
                                 // beyond BUS200E_SLOT_COUNT is logged, ignored

// Remote-enable state at boot. The 200e etiquette (does a 225e expect modules
// to come up enabled?) is unverified on real hardware -- see the design note.
#ifndef BUS200E_REMOTE_DEFAULT
#define BUS200E_REMOTE_DEFAULT 1
#endif

// Storage cards slave at 0x50|cardLo and speak a 24xx-EEPROM-style protocol.
#define BUS200E_CARD_BASE 0x50

// Read every backup record back off the card and compare it before moving on.
//
// A card write that is ACKed on the wire is not proof the data landed: a
// receiver whose buffer overruns still ACKs in hardware, and a short transfer
// then completes with no error anywhere -- state DONE, error NONE, a third of
// the bank missing. That failure was observed on a real bus on 2026-09-18.
// Verifying turns a silent hole into an abort with verify_failures set.
//
// Costs one extra read pass over the bank, so a backup occupies the bus for
// roughly twice as long. Worth it: a backup nobody checked is not a backup.
// Needs card_read; without it the write is unverified and proceeds as before.
// DEFAULT OFF, deliberately. The read-back uses I2CBB_MasterRead, and the card
// master path has never been verified end to end on real hardware. Shipping
// this on by default in v3.5-rc2 broke whole-bank BACKUP on a live bus: the
// read came back unusable, the compare failed, and the transfer aborted after
// a single record. Turning a safety check on before its own mechanism has been
// exercised converts a working backup into a failing one, which is worse than
// the silent hole it was written to catch.
//
// Turn it on with BUS200E_VERIFY_WRITES=1 once the card read path has been
// confirmed against real hardware.
#ifndef BUS200E_VERIFY_WRITES
#define BUS200E_VERIFY_WRITES 0
#endif

// Decoded operations, for the debug ring and dispatch.
typedef enum {
  BUS200E_OP_NONE = 0,
  BUS200E_OP_RECALL,      // arg = preset number
  BUS200E_OP_SAVE,        // arg = preset number
  BUS200E_OP_REMOTE_EN,
  BUS200E_OP_REMOTE_DIS,
  BUS200E_OP_POLL_DONE,   // pre-PRIMO "polling complete" (no action)
  BUS200E_OP_QUERY,       // mod_addr = queried module; we answer if it is us
  BUS200E_OP_ENUM,        // 0x1B broadcast enumerate: every module answers
  BUS200E_OP_BACKUP,      // mod_addr/card_lo/mem_off populated
  BUS200E_OP_RESTORE,     // mod_addr/card_lo/mem_off populated
  BUS200E_OP_MIDI,        // arg = status byte (phase 2; log only)
  BUS200E_OP_CLOCK,       // arg = 0xF8/0xFA/0xFB/0xFC (phase 2; log only)
  BUS200E_OP_UNKNOWN,     // arg = first frame byte
  BUS200E_OP_DROPPED,     // frame poisoned/truncated/preempted; arg = length
} Bus200eOp;

typedef struct {
  uint8_t  op;        // Bus200eOp
  uint8_t  arg;       // preset number / status byte / first byte, per op
  uint8_t  mod_addr;  // target module address (BACKUP/RESTORE/QUERY)
  uint8_t  card_lo;   // card address low byte (BACKUP/RESTORE)
  uint16_t mem_off;   // card memory offset (BACKUP/RESTORE)
} Bus200eCmd;

// Action callbacks. Any pointer (or the whole struct) may be NULL: the
// corresponding action is skipped while logging continues.
typedef struct {
  // Live-state save/recall, the bus equivalent of the front-panel gesture.
  void (*save_preset)(uint8_t slot);
  void (*recall_preset)(uint8_t slot);
  // Raw EEPROM slot records for card transfers (blob = records verbatim).
  int (*slot_read)(uint8_t slot, StoredProgram *out);          // 0 = ok
  int (*slot_write)(uint8_t slot, const StoredProgram *in);    // 0 = ok
  // Card master transfers (i2c_bb-backed on target). `off` is a card memory
  // offset; the implementation encodes it on the wire. 0 = ok.
  int (*card_write)(uint8_t card7, uint32_t off, const uint8_t *d, uint32_t n);
  int (*card_read)(uint8_t card7, uint32_t off, uint8_t *d, uint32_t n);
  // Raw general-call master write: put these bytes on the bus addressed to
  // 0x00, once, when the bus is quiet. Used for the QUERY / enumerate reply,
  // which is a frame rather than an EEPROM-style offset+data transfer, so it
  // cannot go through card_write. NULL = this build never answers a QUERY.
  int (*bus_write)(const uint8_t *d, uint32_t n);               // 0 = ok
} Bus200eOps;

typedef struct {
  uint32_t frames;           // general-call frames parsed
  uint32_t dropped;          // frames discarded (poisoned/preempted/overlong)
  uint32_t job_errors;       // card transfers aborted on an ops error
  uint32_t restore_rejects;  // restore records that failed validation
  uint32_t verify_failures;  // backup records that read back WRONG: a real
                             // bad write, and the job aborts
  uint32_t verify_unavailable; // backup records that could not be read back at
                             // all. NOT a bad write -- the write was ACKed and
                             // we simply could not confirm it, so the transfer
                             // continues unverified rather than discarding a
                             // probably-good backup.
} Bus200eStats;

// Reset all state (parser, remote-enable, job, log, stats) and store `ops`.
// NULL ops = RX-log-only.
void Bus200eInit(const Bus200eOps *ops);

// Feed one transport event. Call from the superloop, never from an ISR.
void Bus200eFeedEvent(uint16_t ev);

// Run pending card backup/restore work: one preset record per call, so a full
// transfer never blocks the superloop for more than ~one record's bus time.
void Bus200eTask(void);

int Bus200eRemoteEnabled(void);
int Bus200eJobActive(void);
const Bus200eStats *Bus200eGetStats(void);

// Debug ring of decoded commands (the RX-log build's whole output).
// Bus200eLogTotal() counts every command since init; Bus200eLogRead(n, out)
// fetches the n-th most recent (0 = latest) if it is still in the ring.
#define BUS200E_LOG_SIZE 32
uint32_t Bus200eLogTotal(void);
int Bus200eLogRead(uint32_t n_back, Bus200eCmd *out);   // 1 = ok, 0 = gone

#endif
