/* This file, together with aes.h, are taken as is from Francis Stokes' github
repo at: https://github.com/francisrstokes/AES-C. 

This file contains all of the operations needed to perform 128-bit encryption and decryption, as Francis implemented.
These operations are neatly explained here: https://www.youtube.com/watch?v=OnhtzFJW_4I&list=PLP29wDx6QmW7HaCrRydOnxcy8QmW0SNdQ&index=18.AES__H

This file implements Galois Field multiplication, addition, substitution boxes, Round constant coloumns, a function to compute
the key schedule, functions for rotating words, substituting bytes and words, shifting rows and its inverse, mix columns, add
round key, encrypt and decrypt a block

According to Francis, this is the most literal translation of the AES specification to code. It isn't necessarily efficient.
On some platforms there would be a processor instruction that will do many of these actions in a single instruction.
 */

#include "aes.h"

// For memcpy
#include "string.h"

uint8_t GF_Mult(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  uint8_t shiftEscapesField = 0;

  // Loop through byte `b`
  for (uint8_t i = 0; i < 8; i++) {
    // If the LSB is set (i.e. we're not multiplying out by zero for this polynomial term)
    // then we xor the result with `a` (i.e. adding the polynomial terms of a)
    if (b & 1) {
      result ^= a;
    }

    // Double `a`, keeping track of whether that causes `a` to leave the field.
    shiftEscapesField = a & 0x80;
    a <<= 1;

    // Since the next bit we look at in `b` will represent multiplying the terms in `a`
    // by the next power of 2, we can achieve the same result by shifting `a` left.
    // If `a` left the field, we need to modulo with irreduciable polynomial term.
    if (shiftEscapesField) {
      // Note that we use 0x1b instead of 0x11b. If we weren't taking advantage of
      // u8 overflow (i.e. by using u16, we would use the "real" term)
      a ^= 0x1b;
    }

    // Shift `b` down in order to look at the next LSB (worth twice as much in the multiplication)
    b >>= 1;
  }

  return result;
}

void GF_WordAdd(AES_Column_t a, AES_Column_t b, AES_Column_t dest) {
  dest[0] = a[0] ^ b[0];
  dest[1] = a[1] ^ b[1];
  dest[2] = a[2] ^ b[2];
  dest[3] = a[3] ^ b[3];
}

// Spec page 16
const uint8_t sbox_encrypt[] = {
/*          0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f */
/* 0 */  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
/* 1 */  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
/* 2 */  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
/* 3 */  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
/* 4 */  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
/* 5 */  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
/* 6 */  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
/* 7 */  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
/* 8 */  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
/* 9 */  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
/* a */  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
/* b */  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
/* c */  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
/* d */  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
/* e */  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
/* f */  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

// Spec page 22
const uint8_t sbox_decrypt[] = {
/*          0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f */
/* 0 */  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
/* 1 */  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
/* 2 */  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
/* 3 */  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
/* 4 */  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
/* 5 */  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
/* 6 */  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
/* 7 */  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
/* 8 */  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
/* 9 */  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
/* a */  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
/* b */  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
/* c */  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
/* d */  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
/* e */  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
/* f */  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

// Spec Appendix A1
AES_Column_t Rcon[] = {
  { 0x01, 0x00, 0x00, 0x00 },
  { 0x02, 0x00, 0x00, 0x00 },
  { 0x04, 0x00, 0x00, 0x00 },
  { 0x08, 0x00, 0x00, 0x00 },
  { 0x10, 0x00, 0x00, 0x00 },
  { 0x20, 0x00, 0x00, 0x00 },
  { 0x40, 0x00, 0x00, 0x00 },
  { 0x80, 0x00, 0x00, 0x00 },
  { 0x1b, 0x00, 0x00, 0x00 },
  { 0x36, 0x00, 0x00, 0x00 },
};

void AES_KeySchedule128(const AES_Key128_t key, AES_Block_t* keysOut) {
  // Track which round key we're on
  AES_Block_t* currentRoundKey = keysOut;

  // The first round key is the key itself
  memcpy(currentRoundKey, key, sizeof(AES_Block_t));

  // Point to the first computed round key
  AES_Block_t* nextRoundKey = currentRoundKey + 1;

  // Temporary copy of the 3rd column
  AES_Column_t col3;
  memcpy(col3, (*currentRoundKey)[3], sizeof(AES_Column_t));

  for (size_t i = 0; i < NUM_ROUND_KEYS_128 - 1; i++) {
    // Modify the last column of the round key
    AES_RotWord(col3);
    AES_SubWord(col3, sbox_encrypt);
    GF_WordAdd(col3, Rcon[i], col3);

    // Compute the next round key
    GF_WordAdd(col3,               (*currentRoundKey)[0], (*nextRoundKey)[0]);
    GF_WordAdd((*nextRoundKey)[0], (*currentRoundKey)[1], (*nextRoundKey)[1]);
    GF_WordAdd((*nextRoundKey)[1], (*currentRoundKey)[2], (*nextRoundKey)[2]);
    GF_WordAdd((*nextRoundKey)[2], (*currentRoundKey)[3], (*nextRoundKey)[3]);

    // Update the last column for the next round
    memcpy(col3, (*nextRoundKey)[3], sizeof(AES_Column_t));

    // Move the current and next round key pointers
    currentRoundKey++;
    nextRoundKey++;
  }
}

void AES_RotWord(AES_Column_t word) {
  uint8_t temp = word[0];
  word[0] = word[1];
  word[1] = word[2];
  word[2] = word[3];
  word[3] = temp;
}

void AES_SubBytes(AES_Block_t state, const uint8_t table[]) {
  uint8_t index;
  for (size_t col = 0; col < 4; col++) {
    for  (size_t row = 0; row < 4; row++) {
      index = state[col][row];
      state[col][row] = table[index];
    }
  }
}

void AES_SubWord(AES_Column_t word, const uint8_t table[]) {
  uint8_t index;
  for (size_t i = 0; i < 4; i++) {
    index = word[i];
    word[i] = table[index];
  }
}

void AES_ShiftRows(AES_Block_t state) {
  uint8_t temp0;
  uint8_t temp1;

  // This implementation is a little awkward because of storing columns
  // in each array of the block instead of rows

  // Shift row 1
  // [0] [1] [2] [3] -> [1] [2] [3] [0]
  temp0 = state[0][1];
  state[0][1] = state[1][1];
  state[1][1] = state[2][1];
  state[2][1] = state[3][1];
  state[3][1] = temp0;

  // Shift row 2
  // [0] [1] [2] [3] -> [2] [3] [0] [1]
  temp0 = state[0][2];
  temp1 = state[1][2];
  state[0][2] = state[2][2];
  state[1][2] = state[3][2];
  state[2][2] = temp0;
  state[3][2] = temp1;

  // Shift row 3
  // [0] [1] [2] [3] -> [3] [0] [1] [2]
  temp0 = state[3][3];
  state[3][3] = state[2][3];
  state[2][3] = state[1][3];
  state[1][3] = state[0][3];
  state[0][3] = temp0;
}

void AES_InvShiftRows(AES_Block_t state) {
  uint8_t temp0;
  uint8_t temp1;

  // Shift row 1
  // [0] [1] [2] [3] -> [3] [0] [1] [2]
  temp0 = state[3][1];
  state[3][1] = state[2][1];
  state[2][1] = state[1][1];
  state[1][1] = state[0][1];
  state[0][1] = temp0;

  // Shift row 2
  // [0] [1] [2] [3] -> [2] [3] [0] [1]
  temp0 = state[0][2];
  temp1 = state[1][2];
  state[0][2] = state[2][2];
  state[1][2] = state[3][2];
  state[2][2] = temp0;
  state[3][2] = temp1;

  // Shift row 3
  // [0] [1] [2] [3] -> [1] [2] [3] [0]
  temp0 = state[0][3];
  state[0][3] = state[1][3];
  state[1][3] = state[2][3];
  state[2][3] = state[3][3];
  state[3][3] = temp0;
}

void AES_MixColumns(AES_Block_t state) {
  AES_Column_t temp = { 0 };

  for (size_t i = 0; i < 4; i++) {
    temp[0] = GF_Mult(0x02, state[i][0]) ^ GF_Mult(0x03, state[i][1]) ^ state[i][2] ^ state[i][3];
    temp[1] = state[i][0] ^ GF_Mult(0x02, state[i][1]) ^ GF_Mult(0x03, state[i][2]) ^ state[i][3];
    temp[2] = state[i][0] ^ state[i][1] ^ GF_Mult(0x02, state[i][2]) ^ GF_Mult(0x03, state[i][3]);
    temp[3] = GF_Mult(0x03, state[i][0]) ^ state[i][1] ^ state[i][2] ^ GF_Mult(0x02, state[i][3]);

    state[i][0] = temp[0]; state[i][1] = temp[1]; state[i][2] = temp[2]; state[i][3] = temp[3];
  }
}

void AES_InvMixColumns(AES_Block_t state) {
  AES_Column_t temp = { 0 };

  for (size_t i = 0; i < 4; i++) {
    temp[0] = GF_Mult(0x0e, state[i][0]) ^ GF_Mult(0x0b, state[i][1]) ^ GF_Mult(0x0d, state[i][2]) ^ GF_Mult(0x09, state[i][3]);
    temp[1] = GF_Mult(0x09, state[i][0]) ^ GF_Mult(0x0e, state[i][1]) ^ GF_Mult(0x0b, state[i][2]) ^ GF_Mult(0x0d, state[i][3]);
    temp[2] = GF_Mult(0x0d, state[i][0]) ^ GF_Mult(0x09, state[i][1]) ^ GF_Mult(0x0e, state[i][2]) ^ GF_Mult(0x0b, state[i][3]);
    temp[3] = GF_Mult(0x0b, state[i][0]) ^ GF_Mult(0x0d, state[i][1]) ^ GF_Mult(0x09, state[i][2]) ^ GF_Mult(0x0e, state[i][3]);

    state[i][0] = temp[0]; state[i][1] = temp[1]; state[i][2] = temp[2]; state[i][3] = temp[3];
  }
}

void AES_AddRoundKey(AES_Block_t state, const AES_Block_t roundKey) {
  for (size_t col = 0; col < 4; col++) {
    for  (size_t row = 0; row < 4; row++) {
      state[col][row] ^= roundKey[col][row];
    }
  }
}

void AES_EncryptBlock(AES_Block_t state, const AES_Block_t* keySchedule) {
  AES_Block_t* roundKey = (AES_Block_t*)keySchedule;

  // Initial round key addition
  AES_AddRoundKey(state, *roundKey++);

  // Note that i starts at 1 since the initial round key is applied already
  for (size_t i = 1; i < NUM_ROUND_KEYS_128; i++) {
    AES_SubBytes(state, sbox_encrypt);
    AES_ShiftRows(state);

    // No column mix in the last round. Not that this implementation should be considered for
    // production, but this would constitute a timing based side-channel risk
    if (i < NUM_ROUND_KEYS_128 - 1) {
      AES_MixColumns(state);
    }

    AES_AddRoundKey(state, *roundKey++);
  }
}

void AES_DecryptBlock(AES_Block_t state, const AES_Block_t* keySchedule) {
  AES_Block_t* roundKey = (AES_Block_t*)keySchedule + NUM_ROUND_KEYS_128 - 1;

  // Note that i starts at 1 since the initial round key is applied already
  for (size_t i = 1; i < NUM_ROUND_KEYS_128; i++) {
    AES_AddRoundKey(state, *roundKey--);

    // No column mix in the first round
    if (i != 1) {
      AES_InvMixColumns(state);
    }

    AES_InvShiftRows(state);
    AES_SubBytes(state, sbox_decrypt);
  }

  // Last key addition
  AES_AddRoundKey(state, *roundKey);
}