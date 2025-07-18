#ifndef INC_FIRMWARE_INFO_H
#define INC_FIRMWARE_INFO_H

#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/vector.h>
#include "common-defines.h"

#define ALIGNED(address, alignment) (((address) - 1U + (alignment)) & -(alignment)) // Same calculation as the linkerscript is doing when specifing .ALIGN(alignment)

#define BOOTLOADER_SIZE                         (0x8000U)                           // 32KiB, reserved at the beginning of flash memory for our bootloader
#define MAIN_APP_START_ADDRESS                  (FLASH_BASE + BOOTLOADER_SIZE)      // First address of our bootloader's main application
#define MAX_FW_LENGTH                           ((1024U * 512U) - BOOTLOADER_SIZE)  // 512Kb flash (stm32f446xx)
#define DEVICE_ID  (0x42)                       // Arbitrary value. One byte - allows the system to support 256 different devices. XXXX Arrange all code...

#define FWINFO_ADDRESS                          (ALIGNED((MAIN_APP_START_ADDRESS + sizeof(vector_table_t)), 16))

// The two defintions below ended up not being used. Before "signing" the firmware, we were validating everything after the firmware section - the IVT wasn't
// being taken into account. Once the idea of signing the code, this is being taken care of.
// #define FWINFO_VALIDATE_FROM                    (FWINFO_ADDRESS + sizeof(firmware_info_t))         // Takes us to the first code after the vector table and this firmware_info_t section
// #define FWINFO_VALIDATE_LENGTH(fw_length)       (fw_length - sizeof(vector_table_t) - sizeof(firmware_info_t))  // Our firmware image will contain a vector table
                                                                                                                // and firmware_info_t. We're validating everything
                                                                                                                // execpt those 2 parts

#define SIGNATURE_ADDRESS                       (FWINFO_ADDRESS + sizeof(firmware_info_t))
// XXXX put somewhere: We don't need the crc anymore in firmware_info_t, the AES-CBC-MAC is effectively going to function as a hash for us, and we will compare
// it. If it doesn't match then we're not going to jump to the firmware. If there was an integrity problem, we would catch that in the CBC-MAC as well.

#define FWINFO_SENTINEL                         (0xDEADC0DE)

// This struct will be placed in memory directly after the interrupt vector table.
// The size of the vector table is the same for a particular chip. It may very between STM chips. 
// Can see the size at vector_table_t from vector.h (episode 12 18:20 and probably earlier episodes too)  XXXX put this in the right place

// We want to make sure that the size of the struct is a multiple of 16 bytes - will ease the calculations when implementing AES.
typedef struct firmware_info_t {
    uint32_t sentinel;
    uint32_t device_id; // We're treating it like an 8-bit number. Got extra room for the future
    uint32_t version;
    uint32_t length;
    //uint32_t reserved[4]; // Future-proofing the structure. Not used
    //uint32_t crc32;       // Not used
}firmware_info_t;

#endif  // INC_FIRMWARE_INFO_H