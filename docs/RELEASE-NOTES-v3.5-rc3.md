# MARF v3.5-rc3 — release candidate

**If you flashed rc2 with the 200e bus image, replace it with this one.** rc2
introduced a fault in whole-bank backup to a storage card. Details below.

Everything from rc1 still applies, including the thing that matters most:
**check that your existing presets still load.** The EEPROM layout grew from
16 slots to 30 in rc1 and has not changed since.

If you do not have the 200e bus modification, rc1, rc2 and rc3 behave
identically for you and there is nothing here you need.

## Which file do I flash?

| File | For |
|---|---|
| `marf-…-<date>.hex` (no suffix) | v2 boards. **Start here.** |
| `marf-…-REV1.hex` | original v1.x boards (REV1) |
| `marf-…-v2-200e-bus.hex` | v2 boards **that have had the two-wire 200e bus modification** |

`MARF-Flasher-V2.zip` and `MARF-Flasher-REV1.zip` carry the same two normal
images with a one-click installer. The bus image is `.hex` only, on purpose.

## Fixed since rc2

- **Whole-bank backup to a storage card works again.** rc2 added a read-back
  check on every record written to a card, and turned it on by default. On real
  hardware it aborted the transfer after a single record. It is off by default
  in rc3, which restores the rc1 behaviour.

  Two things were wrong. The check could not tell *"the card did not store
  this"* from *"I could not read the card"*, and treated both as a bad write,
  so a card it could not read aborted a transfer that was very likely fine.
  And it was enabled by default even though the card read path it depends on
  had never been exercised on real hardware. A safety check whose own mechanism
  is untested does not add safety, it adds a new way to fail.

  The check still exists and can be turned on with `BUS200E_VERIFY_WRITES=1`
  once that read path has been confirmed. A record that genuinely reads back
  wrong still aborts; a record that cannot be read now counts as unverified and
  the transfer continues.

## Unchanged from rc2

- **A preset manager will list the MARF.** The module answers an addressed
  QUERY (`0x1A`) and a broadcast enumerate (`0x1B`). Verified again on this
  build against a live bus, alongside a 251e, a 259e, a 257e and a CSR.
- The `ProgramPayload` size is documented correctly (264 bytes, not 262) and
  pinned with a compile-time assert. This only matters if you generate preset
  records off-device; the firmware was always correct.

## Known issues and limits

- **The bus work has been tested on one module, in one case, on one bus.**
- Storage-card backup and restore is **not implemented** as an end-to-end
  feature, and the card *read* path has still never been confirmed on hardware.
  That is precisely what the rc2 fault exposed.
- 200e bus support is v2 only. There is no REV1 bus image.

## What to check, in order of how much it would matter

1. **Your existing presets still load.** Unchanged since rc1, still first.
2. If you have the modification: that your preset manager lists the module, and
   that recall and save work.
3. That the module behaves as it did otherwise, particularly CV output.
