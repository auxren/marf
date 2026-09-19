/*
 * Host unit tests for the 200e preset-bus engine (src/bus200e.c): both command
 * framings, remote-enable gating, slot-range gating, frame hygiene
 * (repeated START, overflow poisoning, overlong frames) and the chunked card
 * backup/restore job against fake ops.
 */
#include <stdio.h>
#include <string.h>

#include "bus200e.h"
#include "storage.h"

extern int g_run, g_fail;   // shared counters defined in test_core.c
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_bus200e_tests(void);

/* ---- fake ops ------------------------------------------------------------ */

static uint8_t save_calls[BUS200E_SLOT_COUNT + 4],
               recall_calls[BUS200E_SLOT_COUNT + 4];
static int n_save, n_recall;

static struct { uint8_t card7; uint32_t off; uint32_t len; uint8_t first; }
       cw_calls[BUS200E_SLOT_COUNT + 4];
static int n_cw;
static struct { uint8_t slot; } sw_calls[BUS200E_SLOT_COUNT + 4];
static int n_sw;
static int n_sr, n_cr;
static int fail_card_write;      /* fail the n-th card_write (1-based; 0 = never) */
static int corrupt_odd_reads;    /* card_read returns invalid records for odd slots */

static void f_save(uint8_t slot) { save_calls[n_save++] = slot; }
static void f_recall(uint8_t slot) { recall_calls[n_recall++] = slot; }

static int f_slot_read(uint8_t slot, StoredProgram *out) {
  memset(out, 0, sizeof(*out));
  out->payload.steps[0].val[0] = slot;   /* recognizable payload */
  marf_stored_program_finalize(out);
  n_sr++;
  return 0;
}

static int f_slot_write(uint8_t slot, const StoredProgram *in) {
  (void) in;
  sw_calls[n_sw++].slot = slot;
  return 0;
}

static int f_card_write(uint8_t card7, uint32_t off, const uint8_t *d, uint32_t n) {
  if (fail_card_write && n_cw + 1 == fail_card_write) return -1;
  cw_calls[n_cw].card7 = card7;
  cw_calls[n_cw].off = off;
  cw_calls[n_cw].len = n;
  cw_calls[n_cw].first = d[0];
  n_cw++;
  return 0;
}

static int f_card_read(uint8_t card7, uint32_t off, uint8_t *d, uint32_t n) {
  (void) card7;
  StoredProgram rec;
  uint32_t slot = (n_cr < BUS200E_SLOT_COUNT) ? (uint32_t) n_cr : 0;
  (void) off;
  memset(&rec, 0, sizeof(rec));
  rec.payload.steps[0].val[0] = (uint8_t) slot;
  marf_stored_program_finalize(&rec);
  if (corrupt_odd_reads && (slot & 1)) rec.crc ^= 0xFFFF;
  if (n > sizeof(rec)) n = sizeof(rec);
  memcpy(d, &rec, n);
  n_cr++;
  return 0;
}

/* Captured general-call master writes (QUERY / enumerate replies). */
static struct { uint8_t buf[16]; uint32_t len; } bw_calls[8];
static int n_bw;
static int fail_bus_write;       /* non-zero: bus_write reports failure */

static int f_bus_write(const uint8_t *d, uint32_t n) {
  if (n_bw < (int) (sizeof(bw_calls) / sizeof(bw_calls[0]))) {
    uint32_t k = (n > sizeof(bw_calls[0].buf)) ? sizeof(bw_calls[0].buf) : n;
    memcpy(bw_calls[n_bw].buf, d, k);
    bw_calls[n_bw].len = n;
  }
  n_bw++;
  return fail_bus_write ? -1 : 0;
}

static const Bus200eOps fake_ops = {
  f_save, f_recall, f_slot_read, f_slot_write, f_card_write, f_card_read,
  f_bus_write,
};

/* Ops with every callback present except the reply path. */
static const Bus200eOps ops_no_bus_write = {
  f_save, f_recall, f_slot_read, f_slot_write, f_card_write, f_card_read,
  NULL,
};

/* The reply every 200e module owes a QUERY, verified byte-exact on real
   hardware 2026-09-18 against a 251e (04 22 5C 1C FF), a 259e (04 22 28 1C FF)
   and a 257e (04 22 11 1C FF): [nBytes][0x22 manager][own addr][0x1C][0xFF]. */
static int reply_is_wellformed(int i) {
  return bw_calls[i].len == 5 &&
         bw_calls[i].buf[0] == 0x04 &&
         bw_calls[i].buf[1] == 0x22 &&
         bw_calls[i].buf[2] == BUS200E_MODULE_ADDR &&
         bw_calls[i].buf[3] == 0x1C &&
         bw_calls[i].buf[4] == 0xFF;
}

static void reset(const Bus200eOps *ops) {
  Bus200eInit(ops);
  n_save = n_recall = n_cw = n_sw = n_sr = n_cr = n_bw = 0;
  fail_card_write = 0;
  fail_bus_write = 0;
  corrupt_odd_reads = 0;
  memset(bw_calls, 0, sizeof(bw_calls));
}

/* Feed one general-call frame: START, payload bytes, STOP. */
static void frame(const uint8_t *bytes, int n) {
  Bus200eFeedEvent(BUS200E_EV_START);
  for (int i = 0; i < n; i++) Bus200eFeedEvent(bytes[i]);
  Bus200eFeedEvent(BUS200E_EV_STOP);
}
#define FRAME(...) do { \
    const uint8_t _f[] = { __VA_ARGS__ }; \
    frame(_f, (int) sizeof(_f)); \
  } while (0)

static int last_op(void) {
  Bus200eCmd c;
  return Bus200eLogRead(0, &c) ? c.op : BUS200E_OP_NONE;
}

/* ========================================================================= */

static void test_primo_recall_save(void) {
  printf("test_primo_recall_save\n");
  reset(&fake_ops);
  FRAME(0x00, 5);
  CHECK(n_recall == 1 && recall_calls[0] == 5);
  CHECK(last_op() == BUS200E_OP_RECALL);
  FRAME(0x01, 3);
  CHECK(n_save == 1 && save_calls[0] == 3);
  CHECK(last_op() == BUS200E_OP_SAVE);
  CHECK(Bus200eLogTotal() == 2);
}

/* The whole point of the attachment: every one of our program slots must be
   both storable and recallable from the bus, in both framings. */
static void test_all_slots_round_trip(void) {
  printf("test_all_slots_round_trip\n");

  /* PRIMO framing: recall then save every slot. */
  reset(&fake_ops);
  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) FRAME(0x00, i);
  CHECK(n_recall == BUS200E_SLOT_COUNT);
  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) CHECK(recall_calls[i] == i);

  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) FRAME(0x01, i);
  CHECK(n_save == BUS200E_SLOT_COUNT);
  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) CHECK(save_calls[i] == i);

  /* pre-PRIMO framing: same coverage. */
  reset(&fake_ops);
  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) FRAME(0x04, 0x00, 0x22, 0x01, i);
  CHECK(n_recall == BUS200E_SLOT_COUNT);
  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) CHECK(recall_calls[i] == i);

  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) FRAME(0x04, 0x00, 0x22, 0x02, i);
  CHECK(n_save == BUS200E_SLOT_COUNT);
  for (uint8_t i = 0; i < BUS200E_SLOT_COUNT; i++) CHECK(save_calls[i] == i);
}

/* A full-bank backup must cover every slot exactly once, in order, at
   consecutive record-sized offsets -- so a card holds the whole module. */
static void test_backup_covers_every_slot(void) {
  printf("test_backup_covers_every_slot\n");
  reset(&fake_ops);
  FRAME(0x2D, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);

  /* Run the job to completion; the cap proves it terminates. */
  for (int guard = 0; guard < BUS200E_SLOT_COUNT + 4 && Bus200eJobActive(); guard++)
    Bus200eTask();

  CHECK(!Bus200eJobActive());
  CHECK(n_cw == BUS200E_SLOT_COUNT);
  for (int i = 0; i < n_cw && i < BUS200E_SLOT_COUNT; i++) {
    CHECK(cw_calls[i].off == (uint32_t) i * sizeof(StoredProgram));
    CHECK(cw_calls[i].len == sizeof(StoredProgram));
  }
}

static void test_pre_primo_recall_save(void) {
  printf("test_pre_primo_recall_save\n");
  reset(&fake_ops);
  FRAME(0x04, 0x00, 0x22, 0x01, 7);   /* recall 7, old framing */
  CHECK(n_recall == 1 && recall_calls[0] == 7);
  FRAME(0x04, 0x00, 0x22, 0x02, 2);   /* save 2, old framing */
  CHECK(n_save == 1 && save_calls[0] == 2);
}

static void test_remote_enable_gating(void) {
  printf("test_remote_enable_gating\n");
  reset(&fake_ops);
  CHECK(Bus200eRemoteEnabled() == BUS200E_REMOTE_DEFAULT);

  FRAME(0x15);                        /* PRIMO remote disable */
  CHECK(!Bus200eRemoteEnabled());
  FRAME(0x00, 4);
  CHECK(n_recall == 0);               /* gated... */
  CHECK(last_op() == BUS200E_OP_RECALL);  /* ...but still logged */

  FRAME(0x14);                        /* PRIMO remote enable */
  CHECK(Bus200eRemoteEnabled());
  FRAME(0x00, 4);
  CHECK(n_recall == 1 && recall_calls[0] == 4);

  FRAME(0x04, 0x00, 0x22, 0x17, 0xFF);  /* pre-PRIMO disable (trailing 0xFF) */
  CHECK(!Bus200eRemoteEnabled());
  FRAME(0x04, 0x00, 0x22, 0x16, 0xFF);  /* pre-PRIMO enable */
  CHECK(Bus200eRemoteEnabled());
  FRAME(0x04, 0x00, 0x22, 0x14, 0xFF);  /* polling complete: logged, no-op */
  CHECK(last_op() == BUS200E_OP_POLL_DONE);
  CHECK(Bus200eRemoteEnabled());
}

// A long frame that fails its own validation must NOT be re-read as a short
// command. Bench-observed 2026-09-06: bit drift turned recalls of presets 8,
// 16 and 24 into SAVE(16), SAVE(32) and SAVE(48) -- silently overwriting preset
// slots. Short commands have fixed lengths; enforcing them makes that
// impossible.
static void test_corrupt_long_frame_never_becomes_a_save(void) {
  reset(&fake_ops);
  /* well-formed long SAVE still works: [04][dest][22][02][slot] */
  FRAME(0x04, 0x00, 0x22, 0x02, 7);
  CHECK(n_save == 1 && save_calls[0] == 7);

  /* 5-byte frames whose length byte does not match are NOT short commands */
  reset(&fake_ops);
  FRAME(0x01, 0x10, 0x22, 0x02, 6);   /* starts 0x01: a short SAVE is 2 bytes */
  FRAME(0x00, 0x10, 0x22, 0x02, 6);   /* starts 0x00: a short RECALL is 2 bytes */
  FRAME(0x08, 0x00, 0x44, 0x02, 6);   /* one-bit-shifted rubbish */
  CHECK(n_save == 0);
  CHECK(n_recall == 0);
  CHECK(Bus200eLogTotal() == 3);      /* all three logged, none acted on */

  /* genuine short commands, at their proper lengths, still work */
  reset(&fake_ops);
  FRAME(0x01, 9);                     /* short SAVE, 2 bytes */
  CHECK(n_save == 1 && save_calls[0] == 9);
  reset(&fake_ops);
  FRAME(0x00, 4);                     /* short RECALL, 2 bytes */
  CHECK(n_recall == 1 && recall_calls[0] == 4);
  reset(&fake_ops);
  FRAME(0x14);                        /* remote enable, 1 byte */
  CHECK(Bus200eRemoteEnabled() == 1);

  /* right command byte, wrong length: refused */
  reset(&fake_ops);
  FRAME(0x01);                        /* SAVE with no argument */
  FRAME(0x14, 0x00);                  /* remote enable with a stray byte */
  CHECK(n_save == 0);
}

static void test_preset_range_gating(void) {
  printf("test_preset_range_gating\n");
  /* We cover the whole bus preset space, so the last bus preset must land. */
  CHECK(BUS200E_SLOT_COUNT >= BUS200E_BUS_PRESETS);

  reset(&fake_ops);
  FRAME(0x00, BUS200E_BUS_PRESETS - 1);     /* last bus preset: ours */
  CHECK(n_recall == 1 && recall_calls[0] == BUS200E_BUS_PRESETS - 1);

  /* Past the end of our slots: logged, never dispatched. */
  reset(&fake_ops);
  FRAME(0x00, BUS200E_SLOT_COUNT);          /* first slot we do not have */
  FRAME(0x01, BUS200E_SLOT_COUNT);          /* ... and the save side */
  FRAME(0x00, 0xFF);                        /* far out of range */
  CHECK(n_recall == 0 && n_save == 0);
  CHECK(Bus200eLogTotal() == 3);            /* all logged all the same */
}

static void test_null_ops_logs_only(void) {
  printf("test_null_ops_logs_only\n");
  reset(NULL);                              /* the RX-log-only configuration */
  FRAME(0x00, 1);
  FRAME(0x01, 2);
  FRAME(0x2D, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);
  CHECK(Bus200eLogTotal() == 3);
  CHECK(Bus200eJobActive());                /* job accepted... */
  Bus200eTask();
  CHECK(!Bus200eJobActive());               /* ...and quietly dropped: no ops */
  /* nothing crashed, nothing dispatched -- that is the test */
}

static void test_backup_job(void) {
  printf("test_backup_job\n");
  reset(&fake_ops);
  FRAME(0x2D, BUS200E_MODULE_ADDR, 0x10, 0x00, 0x02);  /* mem 0x0010, card 2 */
  CHECK(Bus200eJobActive());
  for (int i = 0; i < BUS200E_SLOT_COUNT; i++) Bus200eTask();
  CHECK(!Bus200eJobActive());
  CHECK(n_cw == BUS200E_SLOT_COUNT);
  CHECK(cw_calls[0].card7 == (BUS200E_CARD_BASE | 0x02));
  CHECK(cw_calls[0].off == 0x0010);
  CHECK(cw_calls[1].off == 0x0010 + sizeof(StoredProgram));
  CHECK(cw_calls[0].len == sizeof(StoredProgram));
  Bus200eTask();                            /* idle task call is a no-op */
  CHECK(n_cw == BUS200E_SLOT_COUNT);
}

static void test_backup_other_module_ignored(void) {
  printf("test_backup_other_module_ignored\n");
  reset(&fake_ops);
  FRAME(0x2D, 0x44, 0x00, 0x00, 0x00);      /* a 291e's backup, not ours */
  CHECK(!Bus200eJobActive());
  CHECK(last_op() == BUS200E_OP_BACKUP);    /* observed in the log though */
}

static void test_pre_primo_backup_args(void) {
  printf("test_pre_primo_backup_args\n");
  reset(&fake_ops);
  /* [n, 0x00, 0x22, 0x04, modAddr, cardLo, memLSB, memMSB] */
  FRAME(0x07, 0x00, 0x22, 0x04, BUS200E_MODULE_ADDR, 0x00, 0x34, 0x12);
  CHECK(Bus200eJobActive());
  Bus200eTask();
  CHECK(n_cw == 1 && cw_calls[0].off == 0x1234);
  CHECK(cw_calls[0].card7 == BUS200E_CARD_BASE);
}

static void test_backup_aborts_on_error(void) {
  printf("test_backup_aborts_on_error\n");
  reset(&fake_ops);
  fail_card_write = 3;
  FRAME(0x2D, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);
  for (int i = 0; i < BUS200E_SLOT_COUNT; i++) Bus200eTask();
  CHECK(!Bus200eJobActive());
  CHECK(n_cw == 2);                         /* two good writes, then abort */
  CHECK(Bus200eGetStats()->job_errors == 1);
}

static void test_restore_validates_records(void) {
  printf("test_restore_validates_records\n");
  reset(&fake_ops);
  corrupt_odd_reads = 1;
  FRAME(0x2E, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);
  CHECK(Bus200eJobActive());
  for (int i = 0; i < BUS200E_SLOT_COUNT; i++) Bus200eTask();
  CHECK(!Bus200eJobActive());
  CHECK(n_cr == BUS200E_SLOT_COUNT);
  CHECK(n_sw == BUS200E_SLOT_COUNT / 2);    /* only the even, valid records */
  CHECK(sw_calls[0].slot == 0 && sw_calls[1].slot == 2);
  CHECK(Bus200eGetStats()->restore_rejects == BUS200E_SLOT_COUNT / 2);
}

static void test_second_job_dropped_while_busy(void) {
  printf("test_second_job_dropped_while_busy\n");
  reset(&fake_ops);
  FRAME(0x2D, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);
  FRAME(0x2E, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);  /* while busy */
  CHECK(last_op() == BUS200E_OP_DROPPED);
  for (int i = 0; i < BUS200E_SLOT_COUNT; i++) Bus200eTask();
  CHECK(n_cw == BUS200E_SLOT_COUNT && n_sw == 0);      /* backup ran, not restore */
}

static void test_midi_and_clock_log_only(void) {
  printf("test_midi_and_clock_log_only\n");
  reset(&fake_ops);
  FRAME(0x90, 0x3C, 0x64);                  /* note on */
  CHECK(last_op() == BUS200E_OP_MIDI);
  FRAME(0xF8);                              /* clock tick */
  CHECK(last_op() == BUS200E_OP_CLOCK);
  CHECK(n_recall == 0 && n_save == 0 && !Bus200eJobActive());
}

static void test_frame_hygiene(void) {
  printf("test_frame_hygiene\n");
  reset(&fake_ops);

  /* Repeated START drops the partial frame, keeps the fresh one. */
  Bus200eFeedEvent(BUS200E_EV_START);
  Bus200eFeedEvent(0x00);
  Bus200eFeedEvent(BUS200E_EV_START);
  Bus200eFeedEvent(0x00);
  Bus200eFeedEvent(0x05);
  Bus200eFeedEvent(BUS200E_EV_STOP);
  CHECK(n_recall == 1 && recall_calls[0] == 5);
  CHECK(Bus200eGetStats()->dropped == 1);

  /* Transport overflow poisons the frame. */
  reset(&fake_ops);
  Bus200eFeedEvent(BUS200E_EV_START);
  Bus200eFeedEvent(0x00);
  Bus200eFeedEvent(BUS200E_EV_OVF);
  Bus200eFeedEvent(0x05);
  Bus200eFeedEvent(BUS200E_EV_STOP);
  CHECK(n_recall == 0);
  CHECK(Bus200eGetStats()->dropped == 1);

  /* Overlong frame is dropped, not mis-parsed. */
  reset(&fake_ops);
  Bus200eFeedEvent(BUS200E_EV_START);
  for (int i = 0; i < 40; i++) Bus200eFeedEvent(0x00);
  Bus200eFeedEvent(BUS200E_EV_STOP);
  CHECK(n_recall == 0);
  CHECK(Bus200eGetStats()->dropped == 1);

  /* Bytes outside any frame, and empty frames, are ignored. */
  reset(&fake_ops);
  Bus200eFeedEvent(0x00);
  Bus200eFeedEvent(0x05);
  Bus200eFeedEvent(BUS200E_EV_START);
  Bus200eFeedEvent(BUS200E_EV_STOP);
  CHECK(Bus200eLogTotal() == 0 && n_recall == 0);
}

static void test_unknown_commands_logged(void) {
  printf("test_unknown_commands_logged\n");
  reset(&fake_ops);
  FRAME(0x63, 0x01);                        /* not a known PRIMO command */
  CHECK(last_op() == BUS200E_OP_UNKNOWN);
  FRAME(0x04, 0x00, 0x22, 0x7E, 0xFF);      /* unknown pre-PRIMO subcommand */
  CHECK(last_op() == BUS200E_OP_UNKNOWN);
  CHECK(n_recall == 0 && n_save == 0);
}

static void test_log_ring(void) {
  printf("test_log_ring\n");
  reset(&fake_ops);
  for (int i = 0; i < BUS200E_LOG_SIZE + 4; i++) FRAME(0xF8);
  CHECK(Bus200eLogTotal() == (uint32_t) (BUS200E_LOG_SIZE + 4));
  Bus200eCmd c;
  CHECK(Bus200eLogRead(0, &c) && c.op == BUS200E_OP_CLOCK);
  CHECK(Bus200eLogRead(BUS200E_LOG_SIZE - 1, &c));
  CHECK(!Bus200eLogRead(BUS200E_LOG_SIZE, &c));   /* aged out of the ring */
}

/* ---- QUERY / broadcast-enumerate replies --------------------------------- */

static void test_query_replies_when_addressed(void) {
  printf("test_query_replies_when_addressed\n");
  reset(&fake_ops);
  FRAME(0x04, BUS200E_MODULE_ADDR, 0x22, 0x1A, 0xFF);
  CHECK(last_op() == BUS200E_OP_QUERY);
  CHECK(n_bw == 0);                 /* never answered from the RX path */
  Bus200eTask();
  CHECK(n_bw == 1);
  CHECK(reply_is_wellformed(0));
  Bus200eTask();
  CHECK(n_bw == 1);                 /* exactly one reply per request */
}

static void test_query_for_another_module_is_silent(void) {
  printf("test_query_for_another_module_is_silent\n");
  reset(&fake_ops);
  FRAME(0x04, 0x44, 0x22, 0x1A, 0xFF);     /* a 291e's address, not ours */
  Bus200eTask();
  CHECK(n_bw == 0);
  CHECK(last_op() == BUS200E_OP_QUERY);    /* still observed in the log */
}

static void test_enumerate_replies_regardless_of_address(void) {
  printf("test_enumerate_replies_regardless_of_address\n");
  reset(&fake_ops);
  /* 0x1B is the broadcast counterpart: every module answers, whatever the
     destination byte says. Decoded from 251e and 259e firmware. */
  FRAME(0x04, 0x44, 0x22, 0x1B, 0xFF);
  CHECK(last_op() == BUS200E_OP_ENUM);
  Bus200eTask();
  CHECK(n_bw == 1);
  CHECK(reply_is_wellformed(0));
}

static void test_query_reply_is_not_remote_gated(void) {
  printf("test_query_reply_is_not_remote_gated\n");
  reset(&fake_ops);
  FRAME(0x04, 0x00, 0x22, 0x17, 0xFF);     /* remote DISABLE */
  CHECK(!Bus200eRemoteEnabled());
  FRAME(0x04, BUS200E_MODULE_ADDR, 0x22, 0x1A, 0xFF);
  Bus200eTask();
  /* Real modules answer QUERY with remote-enable off -- a 251e did exactly
     that on 2026-09-18 before any 0x16 had been sent this session. */
  CHECK(n_bw == 1 && reply_is_wellformed(0));
}

static void test_query_without_reply_path_is_safe(void) {
  printf("test_query_without_reply_path_is_safe\n");
  reset(&ops_no_bus_write);
  FRAME(0x04, BUS200E_MODULE_ADDR, 0x22, 0x1A, 0xFF);
  Bus200eTask();                           /* must not crash or wedge */
  CHECK(n_bw == 0);
  CHECK(last_op() == BUS200E_OP_QUERY);
  reset(NULL);
  FRAME(0x04, BUS200E_MODULE_ADDR, 0x22, 0x1A, 0xFF);
  Bus200eTask();
  CHECK(last_op() == BUS200E_OP_QUERY);
}

static void test_query_reply_failure_is_not_retried_forever(void) {
  printf("test_query_reply_failure_is_not_retried_forever\n");
  reset(&fake_ops);
  fail_bus_write = 1;
  FRAME(0x04, BUS200E_MODULE_ADDR, 0x22, 0x1A, 0xFF);
  Bus200eTask();
  CHECK(n_bw == 1);
  Bus200eTask();
  Bus200eTask();
  CHECK(n_bw == 1);      /* a failed reply is dropped, not spun on */
}

static void test_query_does_not_disturb_a_card_job(void) {
  printf("test_query_does_not_disturb_a_card_job\n");
  reset(&fake_ops);
  FRAME(0x07, 0x00, 0x22, 0x04, BUS200E_MODULE_ADDR, 0x00, 0x00, 0x00);
  CHECK(Bus200eJobActive());
  FRAME(0x04, BUS200E_MODULE_ADDR, 0x22, 0x1A, 0xFF);
  for (int i = 0; i < BUS200E_SLOT_COUNT + 4; i++) Bus200eTask();
  CHECK(n_bw == 1 && reply_is_wellformed(0));   /* reply got out */
  CHECK(n_cw == BUS200E_SLOT_COUNT);            /* and every slot still shipped */
  CHECK(!Bus200eJobActive());
}

void run_bus200e_tests(void) {
  test_query_replies_when_addressed();
  test_query_for_another_module_is_silent();
  test_enumerate_replies_regardless_of_address();
  test_query_reply_is_not_remote_gated();
  test_query_without_reply_path_is_safe();
  test_query_reply_failure_is_not_retried_forever();
  test_query_does_not_disturb_a_card_job();
  test_primo_recall_save();
  test_pre_primo_recall_save();
  test_all_slots_round_trip();
  test_backup_covers_every_slot();
  test_remote_enable_gating();
  test_corrupt_long_frame_never_becomes_a_save();
  test_preset_range_gating();
  test_null_ops_logs_only();
  test_backup_job();
  test_backup_other_module_ignored();
  test_pre_primo_backup_args();
  test_backup_aborts_on_error();
  test_restore_validates_records();
  test_second_job_dropped_while_busy();
  test_midi_and_clock_log_only();
  test_frame_hygiene();
  test_unknown_commands_logged();
  test_log_ring();
}
