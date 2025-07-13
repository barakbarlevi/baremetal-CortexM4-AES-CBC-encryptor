#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>

#include "core/system.h"
#include "timer.h"
#include "core/uart.h"

#define BOOTLOADER_SIZE (0x8000U)

#define LED_PORT     (GPIOA)
#define LED_PIN      (GPIO5)

#define UART_PORT     (GPIOA)
#define RX_PIN       (GPIO3)        // UART RX
#define TX_PIN       (GPIO2)        // UART TX

// Very quickly, our original firmware code is going to receive interrupts coming in, for example from sys_tick. 
// At the current situation, we're going to be looking for the interrupt handler at the wrong address. this is 
// Because our binary is a combination of the custom bootloader portion and the original firmware portion.
// We need to tell the main application vector table where it now lives.
static void vector_setup(void) {
    SCB_VTOR = BOOTLOADER_SIZE;
}


static void gpio_setup() {
    rcc_periph_clock_enable(RCC_GPIOA); // Enable the clock to this peripheral
    
    // Driving the pin directly using GPIO
    //gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT,GPIO_PUPD_NONE, LED_PIN | GPIO6); // GPIO6 Not needed

    // Driving the pin using PWM. According to the Alternate function mapping table from the datasheet,
    // if we want to have PA5 drivern by TIM2_CH1, we need to use Alternate Function 01 (AF01). This means TIM2_CH1's output comare.
    // It doesn't necessarily have to be PWM output. It could be pulse mode.
    gpio_mode_setup(LED_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, LED_PIN);
    // This requires to also set the alternate function of this pin
    gpio_set_af(LED_PORT, GPIO_AF1, LED_PIN);


    gpio_mode_setup(UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TX_PIN | RX_PIN);  // We need to use the alternate function mode, to have the GPIO pin serve an alternate purpose (UART)
    gpio_set_af(UART_PORT, GPIO_AF7, TX_PIN | RX_PIN);                          // According to the Alternate Function table (chapter 4 table 11 in the datasheet) 

}


/**
 * @brief Bad timing mechanism. The CPU doesn't do anything while it's waiting
 */
static void delay_cycles(uint32_t cycles) {
    for(uint32_t i = 0; i< cycles; i++) {
        __asm__("nop"); // So the compiler doesn't optimize the loop out
    }
}

int main() {

    vector_setup();

    system_setup();
    gpio_setup();

    // while(1) {
    //     gpio_toggle(LED_PORT, LED_PIN);
    //     delay_cycles(84000000 / 4); // A good place to start if we want to blink and a 1 [sec] rate. Because the for loop takes up instructions too
    //                                 // This is a bad solution because the MCU can't do any useful work while we're exeuting this nop loop.
    // }

    timer_setup();
    uart_setup();

    float duty_cycle =  0.0f;
    timer_pwm_set_duty_cycle(duty_cycle);

    uint64_t start_time = system_get_ticks();
    while(1) {
        if(system_get_ticks() - start_time >= 1*10) {
            // gpio_toggle(LED_PORT, LED_PIN);          // Used when toggling the GPIO
            duty_cycle += 1.0f;
            if(duty_cycle > 100.0f) {
                duty_cycle = 0.0f;
            }
            timer_pwm_set_duty_cycle(duty_cycle);
            
            start_time = system_get_ticks();
        }

        // Do useful work
        
        // Check if we received any bytes on the UART and send something back in response
        if(uart_data_available()) {
            uint8_t data = uart_read_byte();
            uart_write_byte(data + 1);
        }

        // Simulating high workload (to justify our ring buffer)
        // Before implementing the ring buffer, working with the preliminary poor solution, if we pressed two
        // keyboard buttons in quick succession, we would only see the first one appear in the terminal. We wrote the first one,
        // we were still busy spinning, we wrote the second one, and only then did we get back around to check if the UART had
        // any bytes available. We missed input under high workload
        system_delay(1000);

    }
    return 0;
}