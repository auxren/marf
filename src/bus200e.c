// 200e preset-bus engine: frame parser (both command framings), remote-enable
// state, save/recall dispatch and chunked card backup/restore jobs.
// Pure logic -- no hardware includes; see bus200e.h for the contract and
// docs/DESIGN-200e-bus.md for the protocol background.

#if BUS200E_ENABLE

#include <string.h>

#include "bus200e.h"

// Longest meaningful general-call frame is the 8-byte pre-PRIMO card command;
// anything longer than this is not a command we know and is dropped.
#define FRAME_MAX 12

static const Bus200eOps *bus_ops;

// A QUERY (or broadcast enumerate) owes exactly one reply frame. It is queued
// here rather than sent from the RX path: the reply is a mastered write, and
// the bus is by definition not quiet while the request's own STOP is still
// being processed. Bus200eTask() drains it.
static uint8_t reply_pending;

// ---- parser state ----------------------------------------------------------
static uint8_t  frame[FRAME_MAX];
static uint8_t  frame_len;
static uint8_t  in_frame;
static uint8_t  frame_poisoned;   // OVF or overlong: drop at STOP

static uint8_t  remote_enabled;

// ---- card transfer job -----------------------------------------------------
static struct {
  uint8_t  active;
  uint8_t  is_restore;
  uint8_t  card_lo;
  uint8_t  next_slot;
  uint16_t mem_off;
} job;

// ---- debug ring + stats ----------------------------------------------------
static Bus200eCmd  log_ring[BUS200E_LOG_SIZE];
static uint32_t    log_total;
static Bus200eStats stats;

static void log_cmd(const Bus200eCmd *c) {
  log_ring[log_total % BUS200E_LOG_SIZE] = *c;
  log_total++;
}

uint32_t Bus200eLogTotal(void) { return log_total; }

int Bus200eLogRead(uint32_t n_back, Bus200eCmd *out) {
  if (n_back >= log_total || n_back >= BUS200E_LOG_SIZE) return 0;
  *out = log_ring[(log_total - 1 - n_back) % BUS200E_LOG_SIZE];
  return 1;
}

int Bus200eRemoteEnabled(void) { return remote_enabled; }
int Bus200eJobActive(void) { return job.active; }
const Bus200eStats *Bus200eGetStats(void) { return &stats; }

void Bus200eInit(const Bus200eOps *ops) {
  bus_ops = ops;
  frame_len = 0;
  in_frame = 0;
  frame_poisoned = 0;
  remote_enabled = BUS200E_REMOTE_DEFAULT;
  memset(&job, 0, sizeof(job));
  memset(&stats, 0, sizeof(stats));
  memset(log_ring, 0, sizeof(log_ring));
  log_total = 0;
}

// ---- dispatch --------------------------------------------------------------

static void dispatch(Bus200eCmd *c) {
  log_cmd(c);

  switch (c->op) {
    case BUS200E_OP_REMOTE_EN:  remote_enabled = 1; break;
    case BUS200E_OP_REMOTE_DIS: remote_enabled = 0; break;

    case BUS200E_OP_RECALL:
      if (remote_enabled && c->arg < BUS200E_SLOT_COUNT &&
          bus_ops && bus_ops->recall_preset)
        bus_ops->recall_preset(c->arg);
      break;

    case BUS200E_OP_SAVE:
      if (remote_enabled && c->arg < BUS200E_SLOT_COUNT &&
          bus_ops && bus_ops->save_preset)
        bus_ops->save_preset(c->arg);
      break;

    case BUS200E_OP_BACKUP:
    case BUS200E_OP_RESTORE:
      if (c->mod_addr != BUS200E_MODULE_ADDR) break;
      if (job.active) {   // one transfer at a time; a second request is dropped
        Bus200eCmd d = { BUS200E_OP_DROPPED, c->op, 0, 0, 0 };
        log_cmd(&d);
        stats.dropped++;
        break;
      }
      job.active = 1;
      job.is_restore = (c->op == BUS200E_OP_RESTORE);
      job.card_lo = c->card_lo;
      job.mem_off = c->mem_off;
      job.next_slot = 0;
      break;

    // QUERY is addressed; the broadcast form is not. Neither is gated by
    // remote-enable -- real modules answer with remote off, which a 251e did
    // on 2026-09-18 before any 0x16 had been sent, and which both the 251e and
    // 259e disassemblies show explicitly.
    case BUS200E_OP_QUERY:
      if (c->mod_addr != BUS200E_MODULE_ADDR) break;
      reply_pending = 1;
      break;

    case BUS200E_OP_ENUM:
      reply_pending = 1;
      break;

    default:  // POLL_DONE / MIDI / CLOCK / UNKNOWN: log only (phase 2)
      break;
  }
}

// ---- frame parsing ---------------------------------------------------------

static void parse_frame(void) {
  Bus200eCmd c = { BUS200E_OP_NONE, 0, 0, 0, 0 };
  const uint8_t *f = frame;
  uint8_t n = frame_len;

  stats.frames++;

  // Pre-PRIMO framing: [nBytes, modAddr, 0x22, subcmd, args...], where nBytes
  // counts the bytes that follow it. No PRIMO command collides with this shape.
  if (n >= 4 && f[2] == 0x22 && f[0] == n - 1) {
    c.mod_addr = f[1];
    switch (f[3]) {
      case 0x01: c.op = BUS200E_OP_RECALL; c.arg = (n > 4) ? f[4] : 0; break;
      case 0x02: c.op = BUS200E_OP_SAVE;   c.arg = (n > 4) ? f[4] : 0; break;
      case 0x14: c.op = BUS200E_OP_POLL_DONE;  break;
      case 0x16: c.op = BUS200E_OP_REMOTE_EN;  break;
      case 0x17: c.op = BUS200E_OP_REMOTE_DIS; break;
      case 0x1A: c.op = BUS200E_OP_QUERY;      break;
      case 0x1B: c.op = BUS200E_OP_ENUM;       break;
      case 0x04:  // dump presets to card: [.., modAddr, cardLo, memLSB, memMSB]
      case 0x05:  // restore presets from card, same argument order
        if (n >= 8) {
          c.op = (f[3] == 0x04) ? BUS200E_OP_BACKUP : BUS200E_OP_RESTORE;
          c.mod_addr = f[4];
          c.card_lo = f[5];
          c.mem_off = (uint16_t) (f[6] | (f[7] << 8));
        } else {
          c.op = BUS200E_OP_UNKNOWN; c.arg = f[3];
        }
        break;
      default:
        c.op = BUS200E_OP_UNKNOWN; c.arg = f[3];
        break;
    }
    dispatch(&c);
    return;
  }

  // Short framing: first byte is the command.
  //
  // Every short command has a FIXED length, and that must be enforced. A long
  // frame that fails the check above arrives here intact, and without a length
  // test its first byte is re-read as a short command -- which is how a
  // corrupted RECALL becomes a SAVE and silently overwrites a preset slot.
  // Observed on the bench 2026-09-06: sending recalls of presets 8, 16 and 24
  // decoded as SAVE(16), SAVE(32) and SAVE(48). A 5-byte frame beginning 0x01
  // is not a valid 2-byte short SAVE and never could be.
  //
  // Refusing on length costs nothing (a real short command always matches) and
  // turns a silent, destructive mis-decode into an ignored frame.
  switch (f[0]) {
    case 0x00:
      if (n != 2) break;                      /* -> UNKNOWN below */
      c.op = BUS200E_OP_RECALL; c.arg = f[1]; break;
    case 0x01:
      if (n != 2) break;
      c.op = BUS200E_OP_SAVE;   c.arg = f[1]; break;
    case 0x14: if (n != 1) break; c.op = BUS200E_OP_REMOTE_EN;  break;
    case 0x15: if (n != 1) break; c.op = BUS200E_OP_REMOTE_DIS; break;
    case 0x2D:  // dump presets to card: [0x2D, modAddr, memLSB, memMSB, cardLo]
    case 0x2E:  // restore presets from card, same argument order
      if (n == 5) {
        c.op = (f[0] == 0x2D) ? BUS200E_OP_BACKUP : BUS200E_OP_RESTORE;
        c.mod_addr = f[1];
        c.mem_off = (uint16_t) (f[2] | (f[3] << 8));
        c.card_lo = f[4];
      }
      break;
    default:
      if (f[0] & 0x80) {
        // Bus MIDI (status-first) and realtime clock ride the same bus.
        c.op = (f[0] >= 0xF8) ? BUS200E_OP_CLOCK : BUS200E_OP_MIDI;
        c.arg = f[0];
      }
      break;
  }
  // Anything that fell through a length check, or matched nothing, is unknown.
  if (c.op == BUS200E_OP_NONE) { c.op = BUS200E_OP_UNKNOWN; c.arg = f[0]; }
  dispatch(&c);
}

static void drop_frame(uint8_t why_len) {
  Bus200eCmd c = { BUS200E_OP_DROPPED, why_len, 0, 0, 0 };
  log_cmd(&c);
  stats.dropped++;
}

void Bus200eFeedEvent(uint16_t ev) {
  if (ev & BUS200E_EV_OVF) {
    frame_poisoned = 1;
    return;
  }
  if (ev & BUS200E_EV_START) {
    // A new transaction while a frame is open (repeated START, or a lost
    // STOP): the partial frame is not trustworthy -- drop it.
    if (in_frame && frame_len) drop_frame(frame_len);
    in_frame = 1;
    frame_len = 0;
    frame_poisoned = 0;
    return;
  }
  if (ev & BUS200E_EV_STOP) {
    if (in_frame && !frame_poisoned && frame_len > 0 && frame_len <= FRAME_MAX)
      parse_frame();
    else if (in_frame && frame_len)
      drop_frame(frame_len);
    in_frame = 0;
    frame_len = 0;
    frame_poisoned = 0;
    return;
  }
  // data byte
  if (!in_frame) return;
  if (frame_len >= FRAME_MAX) { frame_poisoned = 1; return; }
  frame[frame_len++] = (uint8_t) ev;
}

// ---- card transfer job -----------------------------------------------------

void Bus200eTask(void) {
  // Answer a pending QUERY before touching the card job: it is one short
  // frame and the requester is waiting on it, where a card record takes
  // hundreds of times longer. Dropped rather than retried if the write fails
  // -- the manager re-queries, and a module that spins here would fight the
  // bus it just lost.
  //
  // Deliberately falls through to the job rather than returning. Returning
  // would cost a whole Task call per query, and an enumeration sweep of the
  // address space is ~112 of them -- with 0x1B answered unconditionally, a
  // sweep run during a backup would repeatedly starve the transfer.
  //
  // reply_pending is one flag, not a count: two requests arriving between
  // Task calls yield one reply. That is deliberate. The reply carries no
  // per-request state (it is the same five bytes every time), every module
  // answers a 0x1B at once so the bus is already contended, and a requester
  // that hears nothing re-queries. Coalescing is the friendlier failure on a
  // shared wire than emitting a burst.
  if (reply_pending) {
    reply_pending = 0;
    if (bus_ops && bus_ops->bus_write) {
      static const uint8_t reply[5] = {
        0x04,                     // nBytes: 4 follow
        0x22,                     // destination: the preset manager
        BUS200E_MODULE_ADDR,      // source: us -- the reply's only payload
        0x1C,                     // QUERY reply
        0xFF,                     // fixed filler, not a status or type code
      };
      bus_ops->bus_write(reply, sizeof(reply));
    }
  }

  if (!job.active) return;

  if (!bus_ops ||
      (job.is_restore && !(bus_ops->card_read && bus_ops->slot_write)) ||
      (!job.is_restore && !(bus_ops->card_write && bus_ops->slot_read))) {
    job.active = 0;   // RX-log-only build: transfers are logged, never run
    return;
  }

  uint8_t card7 = (uint8_t) ((BUS200E_CARD_BASE | job.card_lo) & 0x7F);
  uint32_t off = (uint32_t) job.mem_off +
                 (uint32_t) job.next_slot * sizeof(StoredProgram);
  StoredProgram rec;

  if (job.is_restore) {
    if (bus_ops->card_read(card7, off, (uint8_t *) &rec, sizeof(rec)) != 0) {
      stats.job_errors++;
      job.active = 0;
      return;
    }
    // The card blob is opaque to the card but not to us: only records that
    // pass the usual magic/version/CRC check are allowed into the EEPROM.
    if (marf_stored_program_valid(&rec)) {
      if (bus_ops->slot_write(job.next_slot, &rec) != 0) {
        stats.job_errors++;
        job.active = 0;
        return;
      }
    } else {
      stats.restore_rejects++;
    }
  } else {
    // Backup writes the slot verbatim, valid or not; restore validates.
    if (bus_ops->slot_read(job.next_slot, &rec) != 0 ||
        bus_ops->card_write(card7, off, (const uint8_t *) &rec,
                            sizeof(rec)) != 0) {
      stats.job_errors++;
      job.active = 0;
      return;
    }
  }

  job.next_slot++;
  if (job.next_slot >= BUS200E_SLOT_COUNT) job.active = 0;
}

#endif  // BUS200E_ENABLE
