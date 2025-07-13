#include "timer.h"
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>

#define PRESCALER (84)
#define ARR_VALUE (1000)

void timer_setup(void) {
    rcc_periph_clock_enable(RCC_TIM2);  // Enable the clock to this peripheral. According to the Alternate function mapping table in the datasheet, 
                                        // TIM2 can be used to drive the PA5 pin that the onboard LED is connected to.

    // High level timer configuration. Loading the Control Register 1 (CR1) with our desired setup values for the TIM2 peripheral
    timer_set_mode(TIM2,
                   TIM_CR1_CKD_CK_INT,  // No clock division
                   TIM_CR1_CMS_EDGE,    // Edge alignment
                   TIM_CR1_DIR_UP);     // Counting up
    
    // Setting up the PWM itself, by setting the output compare mode
    timer_set_oc_mode(TIM2,
                      TIM_OC1,          // Output Compare channel 1. Using this channel, because that's what the datasheet's Alternate function mapping table
                      TIM_OCM_PWM1);

    // Enable the counter so it will count up. Otherwise the peripheral may be running but the counter still won't increment/decrement
    timer_enable_counter(TIM2);

    // The channel to which we're going to be outputting the Capture and Compare Register to in TIM2
    timer_enable_oc_output(TIM2, TIM_OC1);

    // Next, set up frequncy and resolution of the PWM
    // We didn't set any clock division, so we have a frequncy of 84[MHz] coming into the peripheral. We'll want to divide that down.
    // We'll arbitrarily say we'll want to have a freq of 1[KHz] and 1000 divisions with it (1000 points that we can have for out duty cycle).
    // freq = system_freq / ( (prescaler - 1) * (arr - 1) )    where arr is the auto reload register value (ARR)
    timer_set_prescaler(TIM2, PRESCALER - 1);
    timer_set_period(TIM2, ARR_VALUE - 1);
}

void timer_pwm_set_duty_cycle(float duty_cycle) {
    // duty cycle = (Capture Compare Register / Auto Reload Register) * 100
    const float raw_value = (float)ARR_VALUE * (duty_cycle / 100.0f);
    timer_set_oc_value(TIM2, TIM_OC1, (uint32_t)raw_value);             // Knocking off any decimal places that we may have
}