#include <libopencm3/stm32/memorymap.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include "common-defines.h"
#include "core/uart.h"
#include "core/system.h"
#include "comms.h"
#include "bl-flash.h"
#include "core/simple-timer.h"

#define UART_PORT     (GPIOA)
#define RX_PIN       (GPIO3)        // UART RX
#define TX_PIN       (GPIO2)        // UART TX

#define BOOTLOADER_SIZE        (0x8000U)                         // 32KiB, reserved at the beginning of flash memory for our bootloader
#define MAIN_APP_START_ADDRESS (FLASH_BASE + BOOTLOADER_SIZE)   // First address of our bootloader's main application

// Safety check that we get link error when we are overrunning the 32 KiB we specified for the bootloader
// const uint8_t data[0x8000] = {0};

static void gpio_setup(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_mode_setup(UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TX_PIN | RX_PIN);  // We need to use the alternate function mode, to have the GPIO pin serve an alternate purpose (UART)
    gpio_set_af(UART_PORT, GPIO_AF7, TX_PIN | RX_PIN);                          // According to the Alternate Function table (chapter 4 table 11 in the datasheet) 
}

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
    
    //volatile int x = 0;
    //x++;

    system_setup();
    gpio_setup();
    uart_setup();
    comms_setup();
    // In the setups above we're configuring GPIOs, enabling clocks to peripherals (GPIOs, UART), we set up interrupt handlers in
    // the UART, we have interrupt handlers with systick, etc. If we eventually jump to our main app program, with jump_to_main,
    // those don't magiaclly stop having being configured. Even in the main application, we could end up jumping into an ISR back
    // in the bootloader. That won't happen, because the first thing we do in there is to reset the vector table offset register,
    // which means that we would inadvertently (without intention) jump to an interrupt handler that was defined somewhere else.
    // That may not be a problem if we had a handler that's just configured by default to no-operation, immediately jump back
    // and return. But if we had UART set here in the bootloader, and in the main app, the problem is that if if UART and the ring
    // buffer hasn't been properly configured in the main app, then we could get a byte, that activates our isr, which assumes that
    // we've already set up our ring-buffer and everything else needed, and tries to write into uninitialized memory! We'll have to
    // Teardown, symmetrically to the above set-ups, and before the jump to the main app's main function.

    //// Packet testing. NOTE: I myself didn't conduct the test. It seemed like traditional debugging
    // comms_packet_t packet = {
    //     .length = 1,
    //     .data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
    //     .crc = 0
    // };
    // packet.crc = comms_compute_crc(&packet);
    
    //packet.crc++; // Testing re-transmit 

    //// The few lines below are initial test to see that we can erase and write to flash successfully. The flash dump itself
    //// can be viewed in STM32CubeProgrammer if using ST-Link, or other options such as OpenOCD CLI, J-Flash etc.
    // uint8_t data[1024] = {0};
    // for(uint16_t i = 0; i < 1024; i++) {
    //     data[i] = i & 0xff;
    // }
    // bl_flash_erase_main_application();
    // bl_flash_write(0x08008000, data, 1024);
    // bl_flash_write(0x0800C000, data, 1024);
    // bl_flash_write(0x08010000, data, 1024);
    // bl_flash_write(0x08020000, data, 1024);
    // bl_flash_write(0x08040000, data, 1024);
    // bl_flash_write(0x08060000, data, 1024);
    
    simple_timer_t timer;
    simple_timer_t timer2;
    simple_timer_setup(&timer, 1000, false);
    simple_timer_setup(&timer2, 2000, true);

    while(true) {
        // comms_update();
        // comms_write(&packet);
        // system_delay(500);  // msec

        //// Upon boot, we'll check if anyone is trying to send us a firmware update. We'll choose to employ the systick mechanism.
        //// After timeout, we'll just jump to the main application without updating its firmware
        // simple_timer_t tests:
        // if(simple_timer_has_elapsed(&timer)) {
        //     volatile int x = 0;
        //     x++;    // Logpoint here: "Timer elapsed"
        // }

        // if(simple_timer_has_elapsed(&timer2)) {
        //     simple_timer_reset(&timer);
        // }

    }

    // TODO: Teardown

    jump_to_main();         // Jump to the main function in our application portion

    // Never return, because we're going to route our whole execution to the other "space"'s main program
    return 0;
}