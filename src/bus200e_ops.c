// Target-side glue for the 200e preset bus: binds the engine's action
// callbacks to this module's live program state, its EEPROM program slots and
// the bit-banged I2C master. See bus200e_ops.h.

#if BUS200E_ENABLE

#include <string.h>

#include "bus200e_ops.h"
#include "CAT25512.h"
#include "controller.h"
#include "delays.h"
#include "eprom.h"
#include "i2c_bb.h"
#include "presets.h"
#include "storage.h"

// ---- Storage-card wire parameters -----------------------------------------
// The cards behave like 24xx-series I2C EEPROMs, so a transfer is
// "write the memory offset, then stream data". These three constants are the
// parts of that convention we have NOT been able to confirm against a real
// card yet; they are parameterised here rather than baked into the code.
//
// NEEDS HARDWARE VERIFICATION (see docs/DESIGN-200e-bus.md, open items):
//   * address width: 2 bytes big-endian is standard for 24C32 and larger,
//     which any card holding multi-kB preset blobs must be.
//   * page size: writes may not cross a page boundary; the device wraps
//     within the page instead of carrying, silently corrupting the record.
//     64 bytes is the common value for 24C32/64; 24C128/256 use 64 or 128.
//   * write-cycle time: the device NACKs every access until its internal
//     write completes. 5 ms is the datasheet worst case across the family.
#ifndef BUS200E_CARD_ADDR_BYTES
#define BUS200E_CARD_ADDR_BYTES 2
#endif
#ifndef BUS200E_CARD_PAGE
#define BUS200E_CARD_PAGE 64u
#endif
#ifndef BUS200E_CARD_WRITE_MS
#define BUS200E_CARD_WRITE_MS 5u
#endif

// ---- Live save / recall ----------------------------------------------------
// The bus equivalent of the front-panel gesture. Both go through the same
// controller entry points the modal loops use, so a bus recall and a panel
// recall leave the module in identical state; only the blocking panel
// animations are skipped. The engine has already checked remote-enable and
// the slot range before calling.

static void ops_save_preset(uint8_t slot) {
  ControllerSaveProgramToSlot(slot);
}

static void ops_recall_preset(uint8_t slot) {
  ControllerLoadProgramFromSlot(slot);
}

// ---- Raw slot access, for card backup/restore ------------------------------
// The card blob is our EEPROM records verbatim: backup copies a slot out
// without interpreting it, restore writes one back after the engine has
// validated its magic/version/CRC.

static int ops_slot_read(uint8_t slot, StoredProgram *out) {
  if (slot >= BUS200E_SLOT_COUNT) return -1;
  CAT25512_read_block(eprom_memory.programs[slot].start,
                      (unsigned char *) out,
                      eprom_memory.programs[slot].size);
  return 0;
}

static int ops_slot_write(uint8_t slot, const StoredProgram *in) {
  if (slot >= BUS200E_SLOT_COUNT) return -1;
  CAT25512_write_block(eprom_memory.programs[slot].start,
                       (unsigned char *) in,
                       eprom_memory.programs[slot].size);
  // A restored slot is the user's, exactly as a panel save would be: keep the
  // factory bank from overwriting it on a later bank update.
  FactoryMarkUserSave(slot);
  return 0;
}

// ---- Storage-card transfers (we master the bus) ----------------------------

// Encode a card memory offset into the address bytes that precede the data.
static void card_addr(uint8_t *pre, uint32_t off) {
#if BUS200E_CARD_ADDR_BYTES == 2
  pre[0] = (uint8_t) (off >> 8);
  pre[1] = (uint8_t) (off & 0xFF);
#else
  pre[0] = (uint8_t) (off & 0xFF);
#endif
}

static int ops_card_write(uint8_t card7, uint32_t off, const uint8_t *d,
                          uint32_t n) {
  uint8_t pre[BUS200E_CARD_ADDR_BYTES];

  // Split the record at page boundaries: a 24xx write that crosses one wraps
  // to the start of the same page instead of carrying into the next.
  while (n) {
    uint32_t room = BUS200E_CARD_PAGE - (off % BUS200E_CARD_PAGE);
    uint32_t chunk = (n < room) ? n : room;
    int rc;

    card_addr(pre, off);
    rc = I2CBB_MasterWrite(card7, pre, sizeof(pre), d, chunk);
    if (rc != I2CBB_OK) return rc;

    // The card NACKs everything until its internal write cycle finishes.
    delay_ms(BUS200E_CARD_WRITE_MS);

    off += chunk;
    d   += chunk;
    n   -= chunk;
  }
  return 0;
}

static int ops_card_read(uint8_t card7, uint32_t off, uint8_t *d, uint32_t n) {
  uint8_t pre[BUS200E_CARD_ADDR_BYTES];

  // Sequential reads roll across pages inside the device, so one transfer
  // covers the whole record.
  card_addr(pre, off);
  return I2CBB_MasterRead(card7, pre, sizeof(pre), d, n) == I2CBB_OK ? 0 : -1;
}

// ---- General-call reply (we master the bus, briefly) -----------------------

// A QUERY reply is a bare frame to the general-call address, not an
// EEPROM-style offset+data transfer, so it cannot reuse ops_card_write: that
// one prefixes address bytes and then waits out a write cycle the general
// call has no concept of. One write, no prefix, no delay.
//
// Arbitration matters here in a way it does not for a card transfer. Every
// module that receives a 0x1B answers it, so several masters can start at
// once; the loser must simply drop its reply rather than retry into the
// winner's frame. I2CBB_MasterWrite reports that as an ordinary failure and
// the engine treats a failed reply as spent -- the manager re-queries.
static int ops_bus_write(const uint8_t *d, uint32_t n) {
  return I2CBB_MasterWrite(0x00, NULL, 0, d, n) == I2CBB_OK ? 0 : -1;
}

const Bus200eOps bus200e_target_ops = {
  .save_preset   = ops_save_preset,
  .recall_preset = ops_recall_preset,
  .slot_read     = ops_slot_read,
  .slot_write    = ops_slot_write,
  .card_write    = ops_card_write,
  .card_read     = ops_card_read,
  .bus_write     = ops_bus_write,
};

#endif  // BUS200E_ENABLE
