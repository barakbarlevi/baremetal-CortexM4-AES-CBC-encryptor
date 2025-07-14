#include "comms.h"
#include "core/uart.h"
#include "core/crc8.h"

#define PACKET_BUFFER_LENGTH (8)    // 8 is arbitrarily chosen. Doesn't have to be too large

typedef enum comms_state_t {
    CommsState_Length,
    CommsState_Data,
    CommsState_CRC,
} comms_state_t;

// Consider moving to the comms_packet_t struct
static comms_state_t state = CommsState_Length;
static uint8_t data_byte_count = 0;

static comms_packet_t temporary_packet = { .length = 0, .data = {0}, .crc = 0};
static comms_packet_t retx_packet = { .length = 0, .data = {0}, .crc = 0};              // Re-transmit packet
static comms_packet_t ack_packet = { .length = 0, .data = {0}, .crc = 0};               // ACK packet
static comms_packet_t last_transmitted_packet = { .length = 0, .data = {0}, .crc = 0};  // In case we have to retransmit

// Declarations for an additional ring buffer. This one stores packets.
// Not using the ring buffer data structure that we've already implemented is that this time, the data we're buffering
// isn't just a single byte, but of comms_packet_t.
// NOTE: This is a classic motivation example for c++ templates...
// NOTE: The only time we're ever writing packets is when we're in comms_update().
//       The only time we're ever reading packets is when XXXX another part of the firmware XXXX.
//       As long as it's kept this way, we don't even have to worry about interrupts and ISRs here. We know
//       That the calls to read and write (packet) - corresponding to pulling data out of the ring buffer and pushing data into it,
//       are not expected to have to deal with ocncurrency issues. They will for sure happen sequentially. Since we're doing bare
//       metal rather then dealing with a RTOS, we don't have context switching. If we did, we would have had to deal with concurreny issues.
static comms_packet_t packet_buffer[PACKET_BUFFER_LENGTH];
static uint32_t packet_read_index = 0;
static uint32_t packet_write_index = 0;
static uint32_t packet_buffer_mask = PACKET_BUFFER_LENGTH;

/**
 * @brief Check if packet is specifically either a request retransmittion or an ack packet
 *        Assumption: the packet has a valid CRC
 * @param byte Is a specifier for either retx or ack
 */
static bool comms_is_single_byte_packet(const comms_packet_t* packet, uint8_t byte) {
    if(packet->length != 1) { return false; }
    if(packet->data[0] != byte) { return false; }
    for(uint8_t i = 1; i < PACKET_DATA_LENGTH; i++) {
        if(packet->data[i] != 0xff) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Practically a memcpy. Including <string.h> will cause the embedded binary to pull in unnecessary functions
 *        from the standard C library, which increases code size
 */
static void comms_packet_copy(comms_packet_t* source, comms_packet_t* destination) {
    destination->length = source->length;
    for (uint8_t i = 0; i < PACKET_DATA_LENGTH; i++) {
        destination->data[i] = source->data[i];
    }
    destination->crc = source->crc;    
}

void comms_setup(void) {
    
    // Setting up a request retransmit packet
    retx_packet.length = 1;
    retx_packet.data[0] = PACKET_RETX_DATA0;
    for (uint8_t i = 0; i < PACKET_DATA_LENGTH; i++) {
        retx_packet.data[i] = 0xff;
    }
    retx_packet.crc = comms_compute_crc(&retx_packet);

    // Setting up an ack packet
    ack_packet.length = 1;
    ack_packet.data[0] = PACKET_ACK_DATA0;
    for (uint8_t i = 0; i < PACKET_DATA_LENGTH; i++) {
        ack_packet.data[i] = 0xff;
    }
    ack_packet.crc = comms_compute_crc(&ack_packet);
}

bool comms_packets_available(void) {
    return (packet_read_index != packet_write_index);
}

void comms_write(comms_packet_t* packet) {
    uart_write((uint8_t*)packet, PACKET_LENGTH);
}

void comms_read(comms_packet_t* packet) {
    comms_packet_copy(&packet_buffer[packet_read_index], packet);
    packet_read_index = (packet_read_index + 1) & packet_buffer_mask;       // Increment read index with wrap-around
}

void comms_update(void) {
    while(uart_data_available()) {
        switch(state) {
            case CommsState_Length: {
                temporary_packet.length = uart_read_byte();
                state = CommsState_Data;
            } break;

            case CommsState_Data: {
                temporary_packet.data[data_byte_count++] =  uart_read_byte();
                if(data_byte_count >= PACKET_DATA_LENGTH) {
                    data_byte_count = 0;
                    state = CommsState_CRC;
                }
            } break;

            case CommsState_CRC: {
                temporary_packet.crc = uart_read_byte();                          
                if(temporary_packet.crc != comms_compute_crc(&temporary_packet)) {
                    // Request packet retransmittion
                    comms_write(&retx_packet);
                    state = CommsState_Length;
                    break;
                }
                
                // If we reached this point, we had a valid CRC
                if(comms_is_single_byte_packet(&temporary_packet, PACKET_RETX_DATA0)) {
                    comms_write(&last_transmitted_packet);
                    state = CommsState_Length;
                    break;
                }

                // If we reached this point, we check if the receiced packet is an acknowledgment packet.
                // If so, we don't want to store it in a buffer. If it isn't, we'll transmit an ACK and store it
                if(comms_is_single_byte_packet(&temporary_packet, PACKET_ACK_DATA0)) {
                    state = CommsState_Length;
                    break;
                }

                uint32_t next_write_index = (packet_write_index + 1) & packet_buffer_mask;  // Increment write index with wrap-around
                if (next_write_index == packet_read_index) {
                    __asm__("BKPT #0");
                }                                                                           // For debugging purposes
                comms_packet_copy(&temporary_packet, &packet_buffer[packet_write_index]);   // Writing packet into the ring buffer
                packet_write_index = next_write_index;
                comms_write(&ack_packet);                                                   // Send ACK
                state = CommsState_Length;                                                  // According to our state machine

            } break;

            default: {
                state = CommsState_Length;  // This shouldn't happen
            }
        }
    }
}

uint8_t comms_compute_crc (comms_packet_t* packet) {
    // Casting the structure to a uint8_t pointer, interperting it as a series of bytes in memory.
    // Note: when structs have different data types in them, the compiler will insert padding between different fields,
    // to make sure everything lines up on the memory boundary of the largest member. We're good with our current specific
    // implementation because it has only uint8_t fields, no padding will occur.
    return crc8((uint8_t*)&packet, PACKET_LENGTH - PACKET_CRC_BYTES);
}
