#include "core/system.h"
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/vector.h>
#include <libopencm3/stm32/rcc.h>


static volatile uint64_t ticks = 0;    // volatile - since we're using an interrupt, the compiler dones't have a way of knowing that our code will ever call into the handler function.
                                       // For as far as the compiler knows, this function will never get called, so there's no reason to ever have this varaible.
                                        // Or, if we happened to reference it from some other means, the compiler may think there's no way it could ever change thus replace it with a constant.
                                        // We're telling the compiler - don't get smart, actually do what I tell you to do regarding this var.
                                        // Using 64 bits in order to be able to keep time up to a large number of days instead of 49.7 days.
                                        // But, this is a 32-bit MCU. So any addition, such as the one below, can't take single assembly instruction.
                                        // We can't preform atomic operations on a 64-bit value in this 32-bit MCU.
                                        // Between those two assembly instruction, another interrupt can occur.
                                        // We need to mask off other interrupts.
void sys_tick_handler(void) {
    ticks++;
}



/**
 * @brief static means it will be available only in the current tranlation unit. One can think of the latter as the .c file and the .h files it includes
 *        In this function, we set the CPU clock and frequency
 */
static void rcc_setup() {
    rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_3V3_84MHZ]);
}

/**
 * @brief Establish the systick frequency, enable the interrupt
 */
static void systick_setup(void) {
    systick_set_frequency(SYSTICK_FREQ, CPU_FREQ);
    systick_counter_enable();
    systick_interrupt_enable();
}

uint64_t system_get_ticks(void) {
    return ticks;
}

void system_setup(void) {
    rcc_setup();
    systick_setup();
}

/**
 * @brief Spin for a specified amount of milliseconds
 */
void system_delay(uint64_t milliseconds) {
    uint64_t end_time = system_get_ticks() + milliseconds;
    while(system_get_ticks() < end_time) {
        // Spin. Note: The tick varialbe is volatile, so the compiler doesn't optimize this away
    }
}
