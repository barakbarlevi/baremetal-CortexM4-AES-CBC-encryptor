#include "core/ring-buffer.h"

/**
 * @param size Assumed to be a power of 2
 */
void ring_buffer_setup(ring_buffer_t* rb, uint8_t* buffer, uint32_t size) {
    rb->buffer = buffer;
    rb->read_index = 0;
    rb->write_index = 0;
    rb->mask = size - 1;
}

bool ring_buffer_empty(ring_buffer_t* rb) {
    return rb->read_index == rb->write_index;
}

bool ring_buffer_read(ring_buffer_t* rb, uint8_t* byte) {
    
    // Local copies - even if there are multiple readers, we won't get a collision
    uint32_t local_read_index = rb->read_index;
    uint32_t local_write_index = rb->write_index;
    if(local_read_index == local_write_index) {
        // The buffer is empty and we can't read anything from it
        return false;
    }

    *byte = rb->buffer[local_read_index];
    local_read_index = (local_read_index + 1) & rb->mask;   // If we went off the end, the & operation with our mask will wrap it around to 0
    rb->read_index = local_read_index;

    return true;
}

bool ring_buffer_write(ring_buffer_t* rb, uint8_t byte) {

    // Local copies - We never expect to have multiple writers on the UART peripheral. The read index can change, though it wouldn't in the write 
    // situation because we're in an ISR. But to keep this in mind for other contexts where we execute another piece of code 
    // that is also interacting with the buffer in some way, that we've stabilized our values for the whole time.
    uint32_t local_read_index = rb->read_index;
    uint32_t local_write_index = rb->write_index;

    uint32_t next_write_index = (local_write_index + 1) & rb->mask;

    if(next_write_index == local_read_index) {
        // The buffer isn't large enough to read values out before the end is overriden. 1) Consider making in larger. 2) Have a strategy to deal with it
        // Drop the most recent piece of data!
        // We could have chosen to move both pointer and lose the oldest piece of data. We would have to touch both pointers from one place
        // and this should be avoided.
        // We can't have both the pointers point at the same place because that will mean the buffer is empty and we would have lost all the data
        return false;
    }

    rb->buffer[local_write_index] = byte;
    rb->write_index = next_write_index;
    return true;
}