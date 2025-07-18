#include "core/crc.h"

// If we want to compare the crc with the one calculated in the .ts file on the host, we will use a logpoint.
// If a line of code that we want to break on is optimized away, we cant land on it. Thus the use of the 
// volatile dummy variable

//volatile int x = 0;

uint8_t crc8(uint8_t* data, uint32_t length) {
    uint8_t crc = 0;
    for(uint32_t i = 0; i <length; i++) {
        crc ^= data[i];
        for(uint8_t j = 0; j < 8; j++) {
            if( crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
        //x++; // This remains in the built program. We can use a logpoint here
    }
    return crc;
}

uint32_t crc32(const uint8_t* data, const uint32_t length) {
   uint8_t byte;
   uint32_t crc = 0xffffffff;
   uint32_t mask;

   for (uint32_t i = 0; i < length; i++) {
      byte = data[i];
      crc = crc ^ byte;

      for (uint8_t j = 0; j < 8; j++) {
         mask = -(crc & 1);
         crc = (crc >> 1) ^ (0xedb88320 & mask);
      }
   }

   return ~crc;
}