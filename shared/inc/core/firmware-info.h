#ifndef INC_FIRMWARE_INFO_H
#define INC_FIRMWARE_INFO_H

#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/vector.h>
#include "common-defines.h"

#define BOOTLOADER_SIZE                         (0x8000U)                          // 32KiB, reserved at the beginning of flash memory for our bootloader
#define MAIN_APP_START_ADDRESS                  (FLASH_BASE + BOOTLOADER_SIZE)     // First address of our bootloader's main application
#define MAX_FW_LENGTH                           ((1024U * 512U) - BOOTLOADER_SIZE) // 512Kb flash (stm32f446xx)
#define DEVICE_ID  (0x42)                       // Arbitrary value. One byte - allows the system to support 256 different devices. XXXX Arrange all code...

#define FWINFO_ADDRESS                          (MAIN_APP_START_ADDRESS + sizeof(vector_table_t))
#define FWINFO_VALIDATE_FROM                    (FWINFO_ADDRESS + sizeof(firmware_info_t))         // Takes us to the first code after the vector table and this firmware_info_t section
#define FWINFO_VALIDATE_LENGTH(fw_length)       (fw_length - sizeof(vector_table_t) - sizeof(firmware_info_t))  // Our firmware image will contain a vector table
                                                                                                                // and firmware_info_t. We're validating everything
                                                                                                                // execpt those 2 parts
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