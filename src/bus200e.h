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
// UNCONFIRMED: chosen to dodge every address in the 2WIRELESS preset dumps
// (0x10, 0x20, 0x21, 0x28=259, 0x29, 0x32, 0x37, 0x39, 0x44=291e, 0x48, 0x5C)
// but not validated against a real system's enumeration. Verify on the bench
// (a 225e shows a module's address on remote-enable hold) before trusting it.
#ifndef BUS200E_MODULE_ADDR
#define BUS200E_MODULE_ADDR 0x3C
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

// Decoded operations, for the debug ring and dispatch.
typedef enum {
  BUS200E_OP_NONE = 0,
  BUS200E_OP_RECALL,      // arg = preset number
  BUS200E_OP_SAVE,        // arg = preset number
  BUS200E_OP_REMOTE_EN,
  BUS200E_OP_REMOTE_DIS,
  BUS200E_OP_POLL_DONE,   // pre-PRIMO "polling complete" (no action)
  BUS200E_OP_QUERY,       // mod_addr = queried module (response is phase 2)
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
} Bus200eOps;

typedef struct {
  uint32_t frames;           // general-call frames parsed
  uint32_t dropped;          // frames discarded (poisoned/preempted/overlong)
  uint32_t job_errors;       // card transfers aborted on an ops error
  uint32_t restore_rejects;  // restore records that failed validation
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
