#ifndef INC_RING_BUFFER_H
#define INC_RING_BUFFER_H

#include "common-defines.h"

typedef struct ring_buffer_t {
    uint8_t* buffer;
    uint32_t mask;   // Cheap operation to wrap around to the beginning
    uint32_t read_index;
    uint32_t write_index;

    // If we tried to keep track of the number of elements stored in the buffer, some additional work is needed
} ring_buffer_t;

void ring_buffer_setup(ring_buffer_t* rb, uint8_t* buffer, uint32_t size);
bool ring_buffer_empty(ring_buffer_t* rb);                 // Is buffer empty
bool ring_buffer_write(ring_buffer_t* rb, uint8_t byte);   // Write a single byte into the buffer. Return 1 if succesfull, else 0 (if data buffer is full)
bool ring_buffer_read(ring_buffer_t* rb, uint8_t* byte);   // Read a single byte. Return 1 if succesfull, else 0. Write the value out to a pointer



#endif  // INC_RING_BUFFER_H