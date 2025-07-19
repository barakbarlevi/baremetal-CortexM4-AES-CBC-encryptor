#include "core/firmware-info.h"

__attribute__ ((section(".firmware_info"))) // Specific to gcc. Specifying that the structure below is
                                            // a part of this section in memory. Otherwise, will be optimized out
                                            // Make sure to KEEP in the linkerscript
firmware_info_t firmware_info = {
    .sentinel  = FWINFO_SENTINEL,
    .device_id = DEVICE_ID,
    .version   = 0xffffffff,
    .length    = 0xffffffff,
};

__attribute__ ((section(".firmware_signature")))    // Same as the attribute above. Make sure to KEEP in the linkerscript
uint8_t firmware_signature[16] = {0};   // 16 for AES key size

// In the linkerscrpit, line: . = ALIGN(16);
// Noramlly, when the linker is putting everything into memory, it'll take a section, for example of 100 bytes, and place
// it somewhere. Then would place another section directly after the last one. We're telling it to instead of placing it
// on the 100 byte mark, instead place it in the next multiple of 16. Pad the space in between. In this case, we do it
// because as we go through doing the AES CBC, everything is in a neat 16 byte boundary.