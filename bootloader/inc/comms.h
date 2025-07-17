#ifndef INC_COMMS_H
#define INC_COMMS_H

#include "common-defines.h"
#define PACKET_DATA_LENGTH  (16)
#define PACKET_LENGTH_BYTES (1)
#define PACKET_CRC_BYTES    (1)
#define PACKET_LENGTH       (PACKET_DATA_LENGTH + PACKET_LENGTH_BYTES + PACKET_CRC_BYTES)

#define PACKET_RETX_DATA0   (0x19 )                 // Arbitrarily chosen
#define PACKET_ACK_DATA0    (0x15 )                 // Arbitrarily chosen

#define BL_PACKET_SYNC_OBSERVED_DATA0      (0x20)   // BL_PACKET prefix suggest the higher level description
                                                       // packets as explained in the first minutes of episode 10. Value is arbitrarily chosen
#define BL_PACKET_FW_UPDATE_REQ_DATA0      (0x31)   // REQ for request. Ask the host to initiate the process
#define BL_PACKET_FW_UPDATE_RES_DATA0      (0x37)   // RES for response
#define BL_PACKET_DEVICE_ID_REQ_DATA0      (0x3C)   // REQ for request. To make sure Device ID is valid
#define BL_PACKET_DEVICE_ID_RES_DATA0      (0x3F)   // RES for response
#define BL_PACKET_FW_LENGTH_REQ_DATA0      (0x42)   // REQ for response. To make sure we'll have enough memory for the update
#define BL_PACKET_FW_LENGTH_RES_DATA0      (0x45)   // RES for response
#define BL_PACKET_READY_FOR_DATA_DATA0     (0x48)   // Ready to receive firmware data packet
#define BL_PACKET_UPDATE_SUCCESSFUL_DATA0  (0x54)   // Final packet in the process
#define BL_PACKET_NACK_DATA0               (0x59)   // "Protocl level" NACK. When we send this, we're saying: whatever you
                                                    // did, it's not good, we're not continuing, can't recover from this

typedef struct comms_packet_t {     
    uint8_t length;     
    uint8_t data[PACKET_DATA_LENGTH];               // 16 bytes of data
    uint8_t crc;        
} comms_packet_t;       

void comms_setup(void);                                // Setting up the packet state machine
bool comms_packets_available(void);   
void comms_write(comms_packet_t* packet);              // Sending a packet
void comms_read(comms_packet_t* packet);               // Assumption: we used comms_packets_available() to make sure there's a packet to read
void comms_update(void);                               // Communications related workload in the main while(1) loop
uint8_t comms_compute_crc (comms_packet_t* packet);    // Compute the CRC for a packet that has its length and its data set up
bool comms_is_single_byte_packet(const comms_packet_t* packet, uint8_t byte);    // Doxygen style comment block in comms.c
void comms_create_single_byte_packet(comms_packet_t* packet, uint8_t byte);      // As name suggests

#endif  // INC_COMMS_H