#include "common-defines.h"
#include <libopencm3/stm32/memorymap.h>

#define BOOTLOADER_SIZE        (0x8000U)                         // 32KiB, reserved at the beginning of flash memory for our bootloader
#define MAIN_APP_START_ADDRESS (FLASH_BASE + BOOTLOADER_SIZE)   // First address of our bootloader's main application

// const uint8_t data[0x8000] = {0};

static void jump_to_main(void) {
    
    typedef void (*void_fn)(void);  // A type that represents the idea of a function that takes nothing, returns nothing
    
    // We want to get a pointer to the reset vector
    // Each entry in the Interrupt Vector Table is 4 bytes long (uint32_t)
    uint32_t* reset_vector_entry = (uint32_t*)(MAIN_APP_START_ADDRESS + sizeof(uint32_t)); // The first entry in the table isn't the reset vector, it's what sould become the Stack Pointer. It's the second enter, so we add 4
    uint32_t* reset_vector = (uint32_t*)(*reset_vector_entry);

    void_fn jump_fn = (void_fn)reset_vector;    // We interpert that address as a function and we call it. So we just execution to that place
    jump_fn();
    // XXXX Write also and comment about the other 2 solutions! XXXX
}

int main(void) {
    
    // Safety check that we get link error when we are overrunning the 32 KiB we specified for the bootloader
    // volatile uint8_t x = 0;
    // for(uint32_t i = 0; i < 0x8000; i++) { x += data[i];}

    jump_to_main();

    // Never return, because we're going to route our whole execution to the other "space"'s main program
    return 0;
}