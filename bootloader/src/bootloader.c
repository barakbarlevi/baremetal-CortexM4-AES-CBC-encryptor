#include <libopencm3/stm32/memorymap.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <string.h>
#include "common-defines.h"
#include "core/uart.h"
#include "core/system.h"
#include "comms.h"
#include "bl-flash.h"
#include "core/simple-timer.h"
#include "core/firmware-info.h"
#include "core/crc.h"
#include "aes.h"

#define UART_PORT     (GPIOA)
#define RX_PIN       (GPIO3)        // UART RX
#define TX_PIN       (GPIO2)        // UART TX

// Safety check that we get link error when we are overrunning the 32 KiB we specified for the bootloader
// const uint8_t data[0x8000] = {0};

#define SYNQ_SEQ_0 (0xc4)      // First byte in synchronization sequence. Chosen arbitrarily. These are just bytes that we expect to
#define SYNQ_SEQ_1 (0x55)      // get in a row
#define SYNQ_SEQ_2 (0x7e)
#define SYNQ_SEQ_3 (0x10)

#define DEFAULT_TIMEOUT (5000)  // 5 secs

typedef enum bl_state_t {
    BL_State_Sync,
    BL_State_WaitForUpdateReq, // Req for request
    BL_State_DevideIDReq,       // Req for request
    BL_State_DevideIDRes,       // Res for response
    BL_State_FWLengthReq,       // Req for request
    BL_State_FWLengthRes,       // Res for response
    BL_State_EraseApplication,
    BL_State_ReceiveFirmware,
    BL_State_Done,
} bl_state_t;

static bl_state_t state = BL_State_Sync;
static uint32_t fw_length = 0;
static uint32_t bytes_written = 0; // Number of firmware update bytes the had been written to flash. To know where our next write goes
static uint8_t sync_seq[4] = {0};  // 4 bytes, initiated at 0
static simple_timer_t timer;
static comms_packet_t temp_packet;  // Will be used both to send and receive. We only do 1 of them at a time

static const uint8_t secret_key[AES_BLOCK_SIZE] = {
    0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f
};  // Right here in text in the firmware! very vulnerable

static void gpio_setup(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_mode_setup(UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TX_PIN | RX_PIN);  // We need to use the alternate function mode, to have the GPIO pin serve an alternate purpose (UART)
    gpio_set_af(UART_PORT, GPIO_AF7, TX_PIN | RX_PIN);                          // According to the Alternate Function table (chapter 4 table 11 in the datasheet) 
}

/***
 * @brief Need to do a teardown (a reverse process to setup) when the bootloader code finishes, cause it interferes with main app setup.
 *        Going in reverse order to the one in the corresponding setup function.
 */
static void gpio_teardown(void) {
    gpio_mode_setup(UART_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, TX_PIN | RX_PIN);  // Changing pins mode AF to ANALOG. Lowest power, default mode for pins.
    rcc_periph_clock_disable(RCC_GPIOA);
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

/**
 * @brief The CBC chaining operation. Take our current state (plaintext block), XOR it with the previous state (first: IV={0}, then CipherText[i]),
 * run through AES, get back out an encrypted block
 */
static void aes_cbc_mac_step(AES_Block_t state, AES_Block_t prev_state, const AES_Block_t *key_schedule) {
    // XOR them together
    for(uint8_t i =0; i < AES_BLOCK_SIZE; i++) {
        ((uint8_t*)state)[i] ^= ((uint8_t*)prev_state)[i];
    }

    AES_EncryptBlock(state, key_schedule);
    
    // Copy whatever comes out of state into prev_state
    memcpy(prev_state, state, AES_BLOCK_SIZE);
}

static bool validate_firmware_image(void) {
    firmware_info_t* firmware_info = (firmware_info_t*)FWINFO_ADDRESS;
    if(firmware_info->sentinel != FWINFO_SENTINEL) { return false; }
    if(firmware_info->device_id != DEVICE_ID) { return false; }

    // // This part got redundant when AES encryption was introduced
    // // At this point this part is valid
    // const uint8_t* start_address = (const uint8_t*)FWINFO_VALIDATE_FROM; // A pointer to where we want to start validating from
    // const uint32_t computed_crc = crc32(start_address, FWINFO_VALIDATE_LENGTH(firmware_info->length));
    // return computed_crc == firmware_info->crc32;

    AES_Block_t round_keys[NUM_ROUND_KEYS_128]; // A block that represents our set of round keys
    AES_KeySchedule128(secret_key, round_keys); // round_keys degrades to a pointer
    // We should have our round keys at this point

    AES_Block_t state = {0};
    AES_Block_t prev_state = {0};   // IV is zeroed, and it's the first "prev_state"
    uint8_t bytes_to_pad = 16 - (firmware_info->length % 16);
    if(bytes_to_pad == 0) { bytes_to_pad = 16; } // Will add an extra block full of 0x10 if the last block was 16-aligned. That's standard. openssl does it

    memcpy(state, firmware_info, AES_BLOCK_SIZE);    // Copying that block (firmware_info) into state
    aes_cbc_mac_step(state, prev_state, round_keys); // Currently, prev_state is the zeroed IV

    uint32_t offset = 0;
    while(offset < firmware_info->length) {
        // Go through the whole length of the firmware

        // Are we at the point where we need to skip the info and the signature sections?
        if( offset == (FWINFO_ADDRESS - MAIN_APP_START_ADDRESS)) {
            // Gets us to being at the offset of FWINFO_ADDRESS
            offset += AES_BLOCK_SIZE * 2; // Skipping two blocks: the fw info and the signature block
            continue;
        }

        // Are we at the last block? (we'll have to pad it/after it)
        if(firmware_info->length - offset > AES_BLOCK_SIZE) {
            // The regular case, not last block
            memcpy(state,(void*)(MAIN_APP_START_ADDRESS + offset), AES_BLOCK_SIZE);
            aes_cbc_mac_step(state, prev_state, round_keys); 
        } else {
            // Last block, needs padding
            if(bytes_to_pad == 16) {
                memcpy(state,(void*)(MAIN_APP_START_ADDRESS + offset), AES_BLOCK_SIZE);
                aes_cbc_mac_step(state, prev_state, round_keys);

                memset(state, bytes_to_pad, AES_BLOCK_SIZE);      // The special case where we add a whole block of padding, cause we were 16-aligned
                aes_cbc_mac_step(state, prev_state, round_keys);
            } else {
                // Just some bytes (not 16) to be padded
            }
        }
    }
    return false;
}

static void bootloading_fail(void) {
    comms_create_single_byte_packet(&temp_packet, BL_PACKET_NACK_DATA0);
    comms_write(&temp_packet);
    state = BL_State_Done;  // Breaking out of the while loop
}

static void check_for_timeout(void) {
    if(simple_timer_has_elapsed(&timer)) {
        bootloading_fail();
    }
}

static bool is_device_id_packet(const comms_packet_t* packet) {
    if(packet->length != 2) { return false; }   // We expect two bytes - the first one specifies that the next one is a device id
    if(packet->data[0] != BL_PACKET_DEVICE_ID_RES_DATA0) { return false; }
    for(uint8_t i = 2; i < PACKET_DATA_LENGTH; i++) {
        if(packet->data[i] != 0xff) {
            return false;
        }
    }
    return true;
}

static bool is_fw_length_packet(const comms_packet_t* packet) {
    if(packet->length != 5) { return false; }   // 5 bytes: the first identifies it as a fw_length packet, the other 4 are a uint32_t length
    if(packet->data[0] != BL_PACKET_FW_LENGTH_RES_DATA0) { return false; }
    for(uint8_t i = 5; i < PACKET_DATA_LENGTH; i++) {
        if(packet->data[i] != 0xff) {
            return false;
        }
    }
    return true;
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
    
    //simple_timer_t timer2;
    simple_timer_setup(&timer, DEFAULT_TIMEOUT, false);
    //simple_timer_setup(&timer2, 2000, true);

    while(state != BL_State_Done) {
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

        if(state == BL_State_Sync) {
            if(uart_data_available()) {
                sync_seq[0] = sync_seq[1];
                sync_seq[1] = sync_seq[2];
                sync_seq[2] = sync_seq[3];
                sync_seq[3] = uart_read_byte();

                bool is_match = sync_seq[0] == SYNQ_SEQ_0;
                is_match = is_match & (sync_seq[1] = SYNQ_SEQ_1);
                is_match = is_match & (sync_seq[2] = SYNQ_SEQ_2);
                is_match = is_match & (sync_seq[3] = SYNQ_SEQ_3);

                if (is_match) {
                    // Sync is observed
                    comms_create_single_byte_packet(&temp_packet, BL_PACKET_SYNC_OBSERVED_DATA0);
                    // Notify the other side
                    comms_write(&temp_packet);
                    // In case we didn't timeout, we also want to reset the timer for the next go-around.
                    // We don't want to have only some amount of secs for the whole process, it might take more
                    simple_timer_reset(&timer);
                    state = BL_State_WaitForUpdateReq;
                } else {
                        check_for_timeout();
                }
            } else {
                check_for_timeout();
            }
            continue;   // To ensure we never get to the next line (state machine) if we haven't already syncd
        }
        // We are assured to have already syncd
        comms_update(); // Takes control of our UART data stream

        switch (state) {
            // BL_State_Sync: addressed above

            case BL_State_WaitForUpdateReq: {

                if(comms_packets_available()) {
                    comms_read(&temp_packet);
                    if(comms_is_single_byte_packet(&temp_packet, BL_PACKET_FW_UPDATE_REQ_DATA0)) {
                        simple_timer_reset(&timer);
                        // Desired situation, we can send our response
                        comms_create_single_byte_packet(&temp_packet, BL_PACKET_FW_UPDATE_RES_DATA0);
                        comms_write(&temp_packet);
                        state = BL_State_DevideIDReq;
                    } else {
                        bootloading_fail(); // The packet we got isn't the one we're looking for at this stage
                    }
                } else {
                    check_for_timeout();
                }

            } break;

            case BL_State_DevideIDReq: {
                
                simple_timer_reset(&timer);
                comms_create_single_byte_packet(&temp_packet, BL_PACKET_DEVICE_ID_REQ_DATA0);
                comms_write(&temp_packet);
                state = BL_State_DevideIDRes;
                
            } break;

            case BL_State_DevideIDRes: {

                if(comms_packets_available()) {
                    comms_read(&temp_packet);
                    if(is_device_id_packet(&temp_packet) && temp_packet.data[1] == DEVICE_ID) {
                        simple_timer_reset(&timer);
                        // device id matched
                        state = BL_State_FWLengthReq;
                    } else {
                        bootloading_fail(); // The packet we got isn't the one we're looking for at this stage
                    }
                } else {
                    check_for_timeout();
                }

                
            } break;

            case BL_State_FWLengthReq: {

                simple_timer_reset(&timer);
                comms_create_single_byte_packet(&temp_packet, BL_PACKET_FW_LENGTH_REQ_DATA0);
                comms_write(&temp_packet);
                state = BL_State_FWLengthRes;

            } break;

            case BL_State_FWLengthRes: {

                if(comms_packets_available()) {
                    comms_read(&temp_packet);

                    // Length data arrives in little endian
                    fw_length = (
                        (temp_packet.data[1])       |
                        (temp_packet.data[2] << 8)  |
                        (temp_packet.data[3] << 16) |
                        (temp_packet.data[4] << 24) 
                    );

                    if(is_fw_length_packet(&temp_packet) && fw_length <= MAX_FW_LENGTH) {
                        // Valid fw length is accepted
                        state = BL_State_EraseApplication;
                    } else {
                        bootloading_fail(); // The packet we got isn't the one we're looking for at this stage
                    }
                } else {
                    check_for_timeout();
                }
                
            } break;

            case BL_State_EraseApplication: {
                bl_flash_erase_main_application();  // May take several seconds
                comms_create_single_byte_packet(&temp_packet, BL_PACKET_READY_FOR_DATA_DATA0);
                comms_write(&temp_packet);
                simple_timer_reset(&timer);         // Sending a packet is a blocking operation, takes time
                state = BL_State_ReceiveFirmware;
            } break;

            case BL_State_ReceiveFirmware: {
                
                if(comms_packets_available()) {
                    comms_read(&temp_packet);

                    // Writing the single packet of data into flash
                    const uint8_t packet_length = (temp_packet.length & 0x0f) + 1;  // We represnt the length of the packet by a full byte, though 4 bits are enough
                    bl_flash_write(MAIN_APP_START_ADDRESS + bytes_written, temp_packet.data, packet_length);
                    bytes_written += packet_length;
                    simple_timer_reset(&timer); // Every time we get a fresh packet we'll reset the timer

                    // If we're done, send the message
                    if(bytes_written >= fw_length) {
                        comms_create_single_byte_packet(&temp_packet, BL_PACKET_UPDATE_SUCCESSFUL_DATA0);
                        comms_write(&temp_packet);
                        state = BL_State_Done;
                    } else{
                        // If we're not done, send that we're ready for some more data
                        comms_create_single_byte_packet(&temp_packet, BL_PACKET_READY_FOR_DATA_DATA0);
                        comms_write(&temp_packet);
                    }

                } else {
                    check_for_timeout();
                }

            } break;

            default: {
                state = BL_State_Sync;  // In an unlikely invalid state
            }

            // No need to address the BL_State_Done case. It's in the while loop condition

        }

    }

    // Teardown: There are a bunch of things we set up in this "bootloader" code. We need to undo them.
    // Before performing the teardown, we need to keep in mind that all 18 bytes of the last uart packet
    // we're sending will be sent before we hit the teardown process. A proper way would be checking to see
    // that we finished sending everything we wanted over uart. A bad implementation would be:
    system_delay(150);  // Should be enough, without the user noticing
    uart_teardown();
    gpio_teardown();
    system_teardown();
    // No comms teardown needed

    if(validate_firmware_image()){
        jump_to_main();         // Jump to the main function in our application portion
    } else {
        // Reset the device
        scb_reset_core();   // ARMv7 and above
    }
    

    // Never return, because we're going to route our whole execution to the other "space"'s main program
    return 0;
}