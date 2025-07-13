BOOTLOADER_SIZE = 0x8000
BOOTLOADER_FILE = "bootloader.bin"
with open(BOOTLOADER_FILE, "rb") as f:
    raw_file=f.read()

bytes_to_pad = BOOTLOADER_SIZE - len(raw_file)  # If the booloader binary was larger than 32 KiB it wouldn't link because we modified the Makefile
padding = bytes([0xff for _ in range(bytes_to_pad)])

with open(BOOTLOADER_FILE, "wb") as f:
    f.write(raw_file + padding)