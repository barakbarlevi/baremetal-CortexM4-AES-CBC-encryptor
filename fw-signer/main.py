#!/usr/bin/env python3

# When we build a firmware image (firmware.bin) it contains the bootloader.
# We don't want to distribute the bootlader, because it contains our secret key.
# We will thus create a temporary firmware image that removes the bootloader (the signatures section is already zeroed out (?))

# What we're signing can be seen at https://youtu.be/Veu_fDPecM8?list=PLP29wDx6QmW7HaCrRydOnxcy8QmW0SNdQ&t=1756
# It'll be the firmware info, IVT, code and data in this order. So this python programm has some shuffling to do

import sys
import os
import subprocess
import struct

BOOTLOADER_SIZE       = 0x8000
FWINFO_OFFSET         = 0x01B0 # This is were DEADC0DE starts in firmware.bin
AES_BLOCK_SIZE        = 16
FWINFO_VERSION_OFFSET = 8  # According to how the fiels in the firmware_info_t struct are ordered
FWINFO_LENGTH_OFFSET  = 12 # According to how the fiels in the firmware_info_t struct are ordered
SIGNATURE_OFFSET      = FWINFO_OFFSET + AES_BLOCK_SIZE

signing_key = "000102030405060708090a0b0c0d0e0f"
zeroed_iv   = "00000000000000000000000000000000"
version_hex = sys.argv[2]
version_value = int(version_hex, base = 16)

if len(sys.argv) < 3:
    print("usage: fw-signer.py <input file> <version number hex>")
    exit(1)

# Read the firmware file
with open(sys.argv[1], "rb") as f:
    f.seek(BOOTLOADER_SIZE) # Chopping off the bootloader
    fw_image = bytearray(f.read()) # Raw firmware image that we get from the build
    f.close()

# Cut out the signature secion, extract out the firmware info section, as the first block to be encrypted:
# This part got left out eventually
# fw_info_section = fw_image[FWINFO_ADDRESS:FWINFO_ADDRESS + AES_BLOCK_SIZE]
# # Check:
# # print(fw_info_section)
# # b'\xde\xc0\xad\xdeB\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff'
# # Looks right

struct.pack_into("<I", fw_image, FWINFO_OFFSET + FWINFO_LENGTH_OFFSET, len(fw_image)) # < for little endian, I for 32-bit unsigned int
struct.pack_into("<I", fw_image, FWINFO_OFFSET + FWINFO_VERSION_OFFSET, version_value) # < for little endian, I for 32-bit unsigned int

signing_image  = fw_image[FWINFO_OFFSET:FWINFO_OFFSET + AES_BLOCK_SIZE]
signing_image += fw_image[:FWINFO_OFFSET] # That should give us the vector table, and then we skip the firmware info section
signing_image += fw_image[FWINFO_OFFSET + AES_BLOCK_SIZE * 2:] # That should give us everything else

signing_image_filename = "image_to_be_signed.bin"
encrypted_filename     = "encrypted_image.bin"

with open(signing_image_filename, "wb") as f:
    f.write(signing_image)
    f.close()
# Checking firmware_to_be_signed.bin in ghex or any other hex editor looks good as expected

# On the host platform we can use openssl to do the aes-128-cbc encryption
openssl_command = f"openssl enc -aes-128-cbc -nosalt -K {signing_key} -iv {zeroed_iv} -in {signing_image_filename} -out {encrypted_filename}"
# encrypted_filename isn't the MAC yet, cause that's just the last 16 bytes
subprocess.call(openssl_command.split(" "))


# Use AES-CBC with a zeroed IV to encrypt every block of the firmware:
# There's an attack that takes advantage of non-zeroed IV, that changes with every update.


# The final block of output is our MAC ! ("signature" - it doesn't comply with being a formal signature, see beginning of episode 13 of the series)
with open(encrypted_filename, "rb") as f:
    f.seek(-AES_BLOCK_SIZE, os.SEEK_END) # Takes us to just shy of the end
    signature = f.read(BOOTLOADER_SIZE) # Raw firmware image that we get from the build
    f.close()

signature_text = ""
for byte in signature:
    signature_text += f"{byte:02x}" # Formats as a 2-digit hex character

print(f"Signed firmware version {version_hex}") 
print(f"key      = {signing_key}") 
print(f"signature= {signature_text}") 

# os.remove(signing_image_filename)
# os.remove(encrypted_filename)

# Patch the signature back into the original firmware image, accounting for the padded length of the encoded firmware. The last block is padded to 16 bytes. If it
# was exactly 16 bytes, it'll be padded with a whole additional block

fw_image[SIGNATURE_OFFSET:SIGNATURE_OFFSET + AES_BLOCK_SIZE] = signature

# Create a new signed file. This is the image we can stream in over the firmware update program
signed_filename = "signed.bin"
with open(signed_filename, "wb") as f:
    f.write(fw_image)
    f.close()
