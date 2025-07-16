#include <libopencm3/stm32/flash.h>
#include "bl-flash.h"

#define MAIN_APP_SECTOR_START (2)   // Sectors 0,1 reserved for our bootloader code portion
#define MAIN_APP_SECTOR_END (7)

void bl_flash_erase_main_application(void) {
    flash_unlock();                 // Writing the right KEY values into the flash key register. Values from reference manual

    for(uint8_t sector = MAIN_APP_SECTOR_START; sector <= MAIN_APP_SECTOR_END; sector++) {
        flash_erase_sector(sector, FLASH_CR_PROGRAM_X32); // Given table 6 in the reference manual and that we don't have
                                                          // an external voltage source, we can only do 32 bits at a time. 32-bit parallelism
                                                          // This libopencm3 function does exactly what the RM specifies
    }

    flash_lock();                   // Setting the bit in the Flash Control Register
}

void bl_flash_write(const uint32_t address, const uint8_t* data, const uint32_t length) {
    flash_unlock();
    flash_program(address, data, length);   // Programs a single byte into the specified address, for every byte in the specified length
    flash_lock();
}
