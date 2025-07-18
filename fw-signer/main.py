#!/usr/bin/env python3

# When we build a firmware image (firmware.bin) it contains the bootloader.
# We don't want to distribute the bootlader, because it contains our secret key.
# We will thus create a temporary firmware image that removes the bootloader (the signatures section is already zeroed out (?))

# What we're signing can be seen at https://youtu.be/Veu_fDPecM8?list=PLP29wDx6QmW7HaCrRydOnxcy8QmW0SNdQ&t=1756
# It'll be the firmware info, IVT, code and data in this order. So this python programm has some shuffling to do

import sys
import os
import subprocess
import ctypes

BOOTLOADER_SIZE   = 0x8000
FWINFO_ADDRESS = 0x01B0 # This is were DEADC0DE starts in firmware.bin
AES_BLOCK_SIZE    = 16

signing_key = "000102030405060708090a0b0c0d0e0f"
zeroed_iv   = "00000000000000000000000000000000"

if len(sys.argv) < 3:
    print("usage: fw-signer.py <input file> <version number hex>")
    exit(1)

# Read the firmware file
with open(sys.argv[1], "rb") as f:
    f.seek(BOOTLOADER_SIZE) # Chopping off the bootloader
    fw_image = f.read(BOOTLOADER_SIZE) # Raw firmware image that we get from the build
    f.close()

# Cut out the signature secion, extract out the firmware info section, as the first block to be encrypted:
# This part got left out eventually
# fw_info_section = fw_image[FWINFO_ADDRESS:FWINFO_ADDRESS + AES_BLOCK_SIZE]
# # Check:
# # print(fw_info_section)
# # b'\xde\xc0\xad\xdeB\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff'
# # Looks right

signing_image  = fw_image[FWINFO_ADDRESS:FWINFO_ADDRESS + AES_BLOCK_SIZE]
signing_image += fw_image[:FWINFO_ADDRESS] # That should give us the vector table, and then we skip the firmware info section
signing_image += fw_image[FWINFO_ADDRESS + AES_BLOCK_SIZE * 2:] # That should give us everything else

signing_image_filename = "image_to_be_signed.bin"
encrypted_filename     = "encrypted_image.bin"

with open(signing_image_filename, "wb") as f:
    f.write(signing_image)
    f.close()
# Checking firmware_to_be_signed.bin in ghex or any other hex editor looks good as expected

# XXXX Something unclear about using openssl
openssl_command = f"openssl enc -aes-128-cbc -nosalt -K {signing_key} -iv {zeroed_iv} -in {signing_image_filename} -out {encrypted_filename}"
# encrypted_filename isn't the MAC yet, cause that's just the last 16 bytes
subprocess.call(openssl_command.split(" "))


# Use AES-CBC with a zeroed IV to encrypt every block of the firmware:
# There's an attack that takes advantage of non-zeroed IV, that changes with every update.


# The final block of output is our MAC ! ("signature" - it doesn't comply with being a formal signature, see beginning of episode 13 of the series)


# Patch info back into the original firmware image, accounting for the padded length of the encoded firmware. The last block is padded to 16 bytes. If it
# was exactly 16 bytes, it'll be padded with a whole additional block
