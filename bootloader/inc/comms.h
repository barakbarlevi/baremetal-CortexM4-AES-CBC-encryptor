#ifndef INC_COMMS_H
#define INC_COMMS_H

#include "common-defines.h"
#define PACKET_DATA_LENGTH  (16)
#define PACKET_LENGTH_BYTES (1)
#define PACKET_CRC_BYTES    (1)
#define PACKET_LENGTH       (PACKET_DATA_LENGTH + PACKET_LENGTH_BYTES + PACKET_CRC_BYTES)

#define PACKET_RETX_DATA0   (0x19 )                 // Arbitrarily chosen
#define PACKET_ACK_DATA0    (0x15 )                 // Arbitrarily chosen

typedef struct comms_packet_t {     
    uint8_t length;     
    uint8_t data[PACKET_DATA_LENGTH];               // 16 bytes of data
    uint8_t crc;        
} comms_packet_t;       

void comms_setup(void);                             // Setting up the packet state machine
bool comms_packets_available(void);
void comms_write(comms_packet_t* packet);           // Sending a packet
void comms_read(comms_packet_t* packet);            // Assumption: we used comms_packets_available() to make sure there's a packet to read
void comms_update(void);                            // Communications related workload in the main while(1) loop
uint8_t comms_compute_crc (comms_packet_t* packet); // Compute the CRC for a packet that has its length and its data set up

#endif  // INC_COMMS_H