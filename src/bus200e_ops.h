#ifndef __BUS200E_OPS_H
#define __BUS200E_OPS_H

#include "bus200e.h"

// ---------------------------------------------------------------------------
// Target-side glue between the (BSP-free) 200e bus engine and this module's
// hardware: the live program state, the external SPI EEPROM's program slots,
// and the bit-banged I2C master used for storage-card transfers.
//
// Everything here runs in superloop context, called from Bus200eFeedEvent /
// Bus200eTask in the controller loop -- never from an ISR.
// ---------------------------------------------------------------------------

extern const Bus200eOps bus200e_target_ops;

#endif
