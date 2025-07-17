#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include "core/uart.h"
#include "core/ring-buffer.h"

#define BAUD_RATE        (115200)
#define RING_BUFFER_SIZE (128)          // For maximum of ~10ms of "latency" (time we can't read from the buffer for), at 115200 baud

//static uint8_t data_buffer = 0U;        // With this poor implementation we have a single byte in our buffer. This means that if we receive data and we
                                        // Haven't had time to read it from the buffer, then it's going to get over written and lost. If we were able
                                        // to send data faster than our firmware has time to deal with it (we may be doing a lot of other "Useful work"
                                        // we'll lose that data. It'll take too much time to reach the uart_write_byte(data + 1); line in the main while
                                        // loop, and by that time we've already received more data and lost the previous.

//static bool data_available = false;     // Using this variable is another problem with this poor implementation. Whenever data come in, and we're
                                        // serving the interrupt, we set it to true. In the main code, when reading, we set it to false. This can collide,
                                        // since we can't expect when the interrupt will come in, and it may come in right before setting it to false,
                                        // thus preventing our system from retreiving valid data from the buffer. That's a race condition between the
                                        // two portions of code over who's going to control this data_available variable.


static ring_buffer_t rb = {0U};
static uint8_t data_buffer[RING_BUFFER_SIZE] = {0U};

// We need to implement the irq handler. The function that we need to implement is from vector.c --> IRQ_HANDLERS --> NVIC_USART2_IRQ --> usart2_isr()
// When we received that inteuupt is when we received a byte. We can either have just received a single normally, or we could have received a byte
// and an overrun could have occured within the peripheral, meaning, more data came into the peripheral than it had time to deal with. If we don't
// get data out of the pripheral fast enough, then it can have its own buffer overflow situation. In order for us to tell which of each of these two
// occured, we need to read the flags from the UART peripheral.
void usart2_isr(void) {
    const bool overrun_occured = usart_get_flag(USART2, USART_FLAG_ORE) == 1;  // Overrun happened or not
    const bool received_data = usart_get_flag(USART2, USART_FLAG_RXNE) == 1;   // Received data or not
    if(received_data || overrun_occured) {
        // First, poor solution
        //data_buffer = (uint8_t)usart_recv(USART2);  // Non sophisticated, will not stand up to possible problems that we'll encounter
        //data_available = true;

        // Using our ring buffer
        if(ring_buffer_write(&rb, (uint8_t)usart_recv(USART2))) {
            // Handle failure. Not so much that we can do at the moment, we probably need to increase buffer size. We can communite it to the program
        }
    }
}


void uart_setup(void) {
    
    ring_buffer_setup(&rb, data_buffer, RING_BUFFER_SIZE);

    // Enable the clock to the peripheral
    // Using the Alternate Funtion Mapping table from the datasheet to pick PA2, PA3, USART2
    rcc_periph_clock_enable(RCC_USART2);

    // A libopencm3 abstration on top of the raw USART registers (can be seen over the reference manual)
    usart_set_mode(USART2, USART_MODE_TX_RX);

    // No hardware flow control
    usart_set_flow_control(USART2, USART_FLOWCONTROL_NONE);

    // Set number of data bits, baudrate, parity bit, number of stop bits (we'll be using 8N1)
    usart_set_databits(USART2, 8);
    usart_set_baudrate(USART2, BAUD_RATE);
    usart_set_parity(USART2, 0);
    usart_set_stopbits(USART2, 1);

    // After setting the registers, now enabling Rx interrupt. We need to enable the ability for interrupts to be wired to this peripheral, using the NVIC
    usart_enable_rx_interrupt(USART2);
    nvic_enable_irq(NVIC_USART2_IRQ);

    // Enable the peripheral itself
    usart_enable(USART2);
}

/**
 * @brief Tearing down the uart_setup function, in reverse order
 */
void uart_teardown(void) {
    
    usart_disable(USART2);  // usart_enable(USART2);

    ring_buffer_setup(&rb, data_buffer, RING_BUFFER_SIZE);

    // Enable the clock to the peripheral
    // Using the Alternate Funtion Mapping table from the datasheet to pick PA2, PA3, USART2
    rcc_periph_clock_enable(RCC_USART2);

    // A libopencm3 abstration on top of the raw USART registers (can be seen over the reference manual)
    usart_set_mode(USART2, USART_MODE_TX_RX);

    // No hardware flow control
    usart_set_flow_control(USART2, USART_FLOWCONTROL_NONE);

    // Set number of data bits, baudrate, parity bit, number of stop bits (we'll be using 8N1)
    usart_set_databits(USART2, 8);
    usart_set_baudrate(USART2, BAUD_RATE);
    usart_set_parity(USART2, 0);
    usart_set_stopbits(USART2, 1);

    // After setting the registers, now enabling Rx interrupt. We need to enable the ability for interrupts to be wired to this peripheral, using the NVIC
    usart_enable_rx_interrupt(USART2);
    nvic_enable_irq(NVIC_USART2_IRQ);

    // Enable the peripheral itself
    
}

void uart_write(uint8_t* data, const uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        uart_write_byte(data[i]);
    }
}

void uart_write_byte(uint8_t data) {
    usart_send_blocking(USART2, (uint16_t)data);    // The casting would have been done implicitly
                                                    // Non blocking - send a transfer out of the UART and NOT wait for it to complete, but rather immediately start executing more code
                                                    // Blocking is easier. If we didn't, we have to enable the transmit complete interrupt and make sure we don't send more data
                                                    // until we received that complete interrupt
}

uint32_t uart_read(uint8_t* data, const uint32_t length) {
    
    // First, poor solution
    // if(length > 0 && data_available) {
    //     *data = data_buffer;
    //     data_available = false; // Because we've read the data that we had
    //     return 1;               // For 1 byte of data
    // }
    // return 0;

    // Implementation with our ring buffer
    if(length == 0) { return 0; }
    for(uint32_t bytes_read = 0; bytes_read < length; bytes_read++) {
        if(!ring_buffer_read(&rb, &data[bytes_read])) {
            // We didn't manage to read a byte. The buffer is empty. Notify the caller of the function that we didn't read the full length of bytes
            // We've read i bytes so far
            return bytes_read;
        }
    }
    return length;
}

uint8_t uart_read_byte(void) {
    
    // First, poor solution
    // data_available = false; // Because we've read the data that we had
    // return data_buffer;

    // Implementation with our ring buffer
    // We dont care if this read is successfull or not, because we've decided that when we do a read_byte it's the user's responsibility
    // to first check if there's data available
    uint8_t byte = 0;
    (void)uart_read(&byte, 1); // The function returns a value, and we want to ignore it. Casting to void to explicitly ignore it
    return byte;
    
}

bool uart_data_available(void) {
    // First, poor solution
    //return data_available;

    // Implementation with our ring buffer
    return !ring_buffer_empty(&rb);
}