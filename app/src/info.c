#include "core/firmware-info.h"

__attribute__ ((section(".firmware_info"))) // Specific to gcc. Specifying that the structure below is
                                            // a part of this section in memory. Otherwise, will be optimized out
firmware_info_t firmware_info = {
    .sentinel  = FWINFO_SENTINEL,
    .length    = 0xffffffff,           // We don't know it yet
    .device_id = DEVICE_ID,
    .crc32     = 0xffffffff,           // We don't know it yet
    .version   = 0xffffffff,
    .reserved0 = 0xffffffff,
    .reserved1 = 0xffffffff,
    .reserved2 = 0xffffffff,
    .reserved3 = 0xffffffff,
    .reserved4 = 0xffffffff,
};