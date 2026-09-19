# MARF v3.5-rc2 — release candidate

Second candidate. Everything in rc1 still applies, including the one thing
that matters most: **check that your existing presets still load.** The EEPROM
layout grew from 16 slots to 30 in rc1 and that has not changed here.

If you are already running rc1 and do not have the 200e bus modification,
there is nothing new for you in this build. The changes are all on the bus.

## Which file do I flash?

| File | For |
|---|---|
| `marf-…-<date>.hex` (no suffix) | v2 boards. **Start here.** |
| `marf-…-REV1.hex` | original v1.x boards (REV1) |
| `marf-…-v2-200e-bus.hex` | v2 boards **that have had the two-wire 200e bus modification** |

`MARF-Flasher-V2.zip` and `MARF-Flasher-REV1.zip` carry the same two normal
images with a one-click installer. The bus image is `.hex` only, on purpose.

## New since rc1

- **A preset manager will now list the MARF.** The module answers the bus's
  discovery commands: an addressed QUERY (`0x1A`) and a broadcast enumerate
  (`0x1B`), replying `[04][22][addr][1C][FF]`, the same frame the real Buchla
  modules send. rc1 obeyed every recall and save while staying invisible to
  enumeration, so a manager scanning the bus to decide what a save would hit
  got an answer that was wrong by one module. **This retires the "will not be
  listed" known issue from rc1.**
- **Backups are now verified.** Every record written to a storage card is read
  back and compared byte for byte. A mismatch aborts the transfer with an
  error instead of reporting success.

## Why the backup change exists

A card write that is acknowledged on the wire is not proof the data arrived.
A receiver whose buffer overruns still acknowledges in hardware and drops the
bytes anyway, and nothing upstream can tell. On a real bus this produced a
backup that returned a third of the bank while reporting complete success,
twice. The cause turned out to be a marginal connector rather than a firmware
defect, but a transfer that cannot tell a full backup from a third of one is
not one you should trust with your presets.

Costs one extra read pass, so a backup occupies the bus about twice as long.

## Known issues and limits

- **The bus work has been tested on one module, in one case, on one bus.**
  Still the main reason this is a candidate rather than a release.
- Storage-card backup and restore over the bus is **not implemented** as an
  end-to-end feature; the verification above covers the transfer path itself.
- 200e bus support is v2 only. There is no REV1 bus image: that pin mapping
  has never been verified for continuity on v1 hardware.

## What to check, in order of how much it would matter

1. **Your existing presets still load.** Unchanged from rc1, and still the
   first thing to confirm.
2. If you have the modification: that your preset manager now *lists* the
   module, and that recall and save still work.
3. That the module still behaves as it did otherwise, particularly CV output.

## Verification behind the bus claims

Hardware-tested on a 200e case with a Studio H WPM, a 259e, a 251e, a 257e and
a Studio H CSR.

| | |
|---|---|
| QUERY sweep of the address space | finds 5 modules, was 4 |
| Reply frame | byte-identical to the 251e, 259e and 257e |
| Empty addresses (`0F`, `42`, `77`) | stay silent, as they should |
| Bus frames received and decoded | 10 / 10, zero errors |
| Failsafe, dropped frames, late edges | 0 |

The empty-address row is the one that makes the rest meaningful: it shows the
sweep can report a failure, so five answers is a result rather than an artefact.
