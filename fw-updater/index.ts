import * as fs from 'fs/promises';
import * as path from 'path';
import {SerialPort} from 'serialport';

// Constants for the packet protocol
const PACKET_LENGTH_BYTES   = 1;
const PACKET_DATA_BYTES     = 16;
const PACKET_CRC_BYTES      = 1;
const PACKET_CRC_INDEX      = PACKET_LENGTH_BYTES + PACKET_DATA_BYTES;
const PACKET_LENGTH         = PACKET_LENGTH_BYTES + PACKET_DATA_BYTES + PACKET_CRC_BYTES;

const PACKET_ACK_DATA0      = 0x15;
const PACKET_RETX_DATA0     = 0x19;

// Bootloader constants
const BL_PACKET_SYNC_OBSERVED_DATA0     = (0x20);
const BL_PACKET_FW_UPDATE_REQ_DATA0     = (0x31);
const BL_PACKET_FW_UPDATE_RES_DATA0     = (0x37);
const BL_PACKET_DEVICE_ID_REQ_DATA0     = (0x3C);
const BL_PACKET_DEVICE_ID_RES_DATA0     = (0x3F);
const BL_PACKET_FW_LENGTH_REQ_DATA0     = (0x42);
const BL_PACKET_FW_LENGTH_RES_DATA0     = (0x45);
const BL_PACKET_READY_FOR_DATA_DATA0    = (0x48);
const BL_PACKET_UPDATE_SUCCESSFUL_DATA0 = (0x54);
const BL_PACKET_NACK_DATA0              = (0x59);

const VECTOR_TABLE_SIZE                 = (0x01B0); // This is were DEADC0DE starts in firmware.bin

const FWINFO_DEVICE_ID_OFFSET           = (VECTOR_TABLE_SIZE + (1 * 4));
const FWINFO_LENGTH_OFFSET              = (VECTOR_TABLE_SIZE + (3 * 4));

const SYNC_SEQ  = Buffer.from([0xc4, 0x55, 0x7e, 0x10]);
const DEFAULT_TIMEOUT  = (60000);

// Details about the serial port connection
const serialPath            = "/dev/ttyACM0";
const baudRate              = 115200;

// CRC8 implementation. Same as the implementation on the target machine
const crc8 = (data: Buffer | Array<number>) => {
  let crc = 0;

  for (const byte of data) {
    crc = (crc ^ byte) & 0xff;  // 0xff to change to 8-bit and not 32-bit
    for (let i = 0; i < 8; i++) {
      if (crc & 0x80) {
        crc = ((crc << 1) ^ 0x07) & 0xff;
      } else {
        crc = (crc << 1) & 0xff;
      }
    }
    //console.log(`0x${byte.toString(16)} 0x${crc.toString(16)}`);        // For debugging purposes
  }

  return crc;
};

const crc32 = (data: Buffer, length: number) => {
  let byte;
  let crc = 0xffffffff;
  let mask;

  for (let i = 0; i < length; i++) {
     byte = data[i];
     crc = (crc ^ byte) >>> 0;

     for (let j = 0; j < 8; j++) {
        mask = (-(crc & 1)) >>> 0;
        crc = ((crc >>> 1) ^ (0xedb88320 & mask)) >>> 0;
     }
  }

  return (~crc) >>> 0;
}

// Async delay function, which gives the event loop time to process outside input
const delay = (ms: number) => new Promise(r => setTimeout(r, ms));

class Logger {
  static info(message: string) { console.log(`[.] ${message}`); }
  static success(message: string) { console.log(`[$] ${message}`); }
  static error(message: string) { console.log(`[!] ${message}`); }
}

// Class for serialising and deserialising packets
class Packet {
  length: number; // TypeScript data type. Has its own functions and methods
  data: Buffer;   // TypeScript data type. Has its own functions and methods
  crc: number;

  static retx = new Packet(1, Buffer.from([PACKET_RETX_DATA0]));
  static ack = new Packet(1, Buffer.from([PACKET_ACK_DATA0]));

  // No need to pass crc when constructing a packet. If crc no passed, it'll automaticall be computed
  constructor(length: number, data: Buffer, crc?: number) {
    this.length = length;
    this.data = data;

    const bytesToPad = PACKET_DATA_BYTES - this.data.length;
    const padding = Buffer.alloc(bytesToPad).fill(0xff);
    this.data = Buffer.concat([this.data, padding]);  // Padding to 16 bytes

    if (typeof crc === 'undefined') {
      this.crc = this.computeCrc();
    } else {
      this.crc = crc;
    }
  }

  computeCrc() {
    const allData = [this.length, ...this.data];
    return crc8(allData);
  }

  // Converts a packet to a Node Buffer
  toBuffer() {
    return Buffer.concat([ Buffer.from([this.length]), this.data, Buffer.from([this.crc]) ]);
  }

  // Same implementation as in C on the target machine
  isSingleBytePacket(byte: number) {
    if (this.length !== 1) return false;
    if (this.data[0] !== byte) return false;
    for (let i = 1; i < PACKET_DATA_BYTES; i++) {
      if (this.data[i] !== 0xff) return false;
    }
    return true;
  }

  isAck() {
    return this.isSingleBytePacket(PACKET_ACK_DATA0);
  }

  isRetx() {
    return this.isSingleBytePacket(PACKET_RETX_DATA0);
  }

  static createSingleBytePacket(byte: number) {
    return new Packet(1, Buffer.from([byte]));
  }
}

// Serial port instance
const uart = new SerialPort({ path: serialPath, baudRate });

// Packet buffer
// Won't implement a ring-buffer in this TypeScript file because we have automatic garbage collection and extending arrays
let packets: Packet[] = [];

let lastPacket: Packet = new Packet(1, Buffer.from([0xff]));  // XXXX EPISODE 7.3 14:20 did he leave this like this?
const writePacket = (packet: Packet) => {
  uart.write(packet.toBuffer());
  console.log(`Inside WritePackeT(). Right after uart.write(packet.toBuffer()); with packet.toBuffer() = ${packet.toBuffer()}`);
  lastPacket = packet;
};

// Serial data buffer, with a splice-like function for consuming data
let rxBuffer = Buffer.from([]);
const consumeFromBuffer = (n: number) => {

  //const consumed = rxBuffer.slice(0, n);
  const consumed = rxBuffer.subarray(0,n);

  //rxBuffer = rxBuffer.slice(n);
  rxBuffer = rxBuffer.subarray(n);

  return consumed;
}

// This function fires whenever data is received over the serial port. The whole
// packet state machine runs here.
uart.on('data', data => {
  
  console.log(`Received ${data.length} bytes through uart`);    // XXXX did he leave this? episode 7.3 26:12 Erased it in episode 11 21:12

  // Add the data to the packet
  rxBuffer = Buffer.concat([rxBuffer, data]);

  console.log(`Building packet`);    // XXXX did he leave this? episode 7.3 26:12

  // Can we build a packet?
  while (rxBuffer.length >= PACKET_LENGTH) {
    const raw = consumeFromBuffer(PACKET_LENGTH); // Will give us a Node Buffer with 18 bytes in it
    // console.log(raw);
    
    //const packet = new Packet(raw[0], raw.slice(1, 1+PACKET_DATA_BYTES), raw[PACKET_CRC_INDEX]);
    const packet = new Packet(raw[0], raw.subarray(1, 1+PACKET_DATA_BYTES), raw[PACKET_CRC_INDEX]);

    const computedCrc = packet.computeCrc();

    // Need retransmission?
    if (packet.crc !== computedCrc) {
      // console.log(`CRC failed, computed 0x${computedCrc.toString(16)}, got 0x${packet.crc.toString(16)}`); // XXXX why did he eventually comment this out? LEAVE ALL OF THESE COMMENTED XXXX
      writePacket(Packet.retx);
      continue;
    }

    // Are we being asked to retransmit?
    if (packet.isRetx()) {
      // console.log(`Retransmitting last packet`); // XXXX why did he eventually comment this out?
      // console.log(`Last packet:`, lastPacket);
      writePacket(lastPacket);
      continue;
    }

    // If this is an ack, move on
    if (packet.isAck()) {
      console.log(`It was an ack, nothing to do`);   // XXXX why did he eventually comment this out?
      continue;
    }

    // If this is an nack, exit the program
    if (packet.isSingleBytePacket(BL_PACKET_NACK_DATA0)) {
      Logger.error('Received NACK. Exiting...');
      // console.log('packets', packets);
      // console.log('uart buffer', rxBuffer);
      process.exit(1);
    }

    // Otherwise write the packet in to the buffer, and send an ack
    console.log(`Storing packet and ack'ing`);   // XXXX why did he eventually comment this out?
    packets.push(packet);
    writePacket(Packet.ack);
  }
});

// Function to allow us to await a packet
const waitForPacket = async (timeout = DEFAULT_TIMEOUT) => {
  let timeWaited = 0;
  while (packets.length < 1) {
    await delay(1);
    timeWaited += 1;

    if (timeWaited >= timeout) {
      throw Error('Timed out waiting for packet'); // Not exiting. We might want to attemp receieve a packets
    }
  }
  return packets.splice(0, 1)[0];
}

const waitForSingleBytePacket = (byte: number, timeout = DEFAULT_TIMEOUT) => (
  waitForPacket(timeout)
    .then(packet => {   // When a packet comes in..
      // Check if it's the packet we're looking for
      if (packet.length !== 1 || packet.data[0] !== byte) {
        const formattedPacket = [...packet.toBuffer()].map(x => x.toString(16)).join(' ');
        throw new Error(`Unexpected packet received. Expected single byte 0x${byte.toString(16)}), got packet ${formattedPacket}`);
      }
    })
    .catch((e: Error) => {
      Logger.error(e.message);
      console.log(rxBuffer);
      console.log(packets);
      process.exit(1);
    })
);

/**
 * @brief Observe the sync sequence: send the sync sequence and get the xxxx
 * @param syncDelay 
 * @param timeout 
 * @returns 
 */
const syncWithBootloader = async (syncDelay = 120000, timeout = DEFAULT_TIMEOUT) => {
  let timeWaited = 0;

  while (true) {
    console.log("Sending SYNC_SEQ:", Buffer.from(SYNC_SEQ));
    uart.write(SYNC_SEQ);
    await delay(syncDelay); // We send a "pulse" of data. The bootloader responds to it immediately, within less than millisecs, and we'll
                            // pick that up. The delay to assure we're not sending them constantly, and then if the bootloader recognizes
                            // and sends one back we're already sending more bytes. If we do that, it'll mess up the syncronization we're trying
                            // to create.
    timeWaited += syncDelay;

    if (packets.length > 0) {
      // Being here means that there is a packet and we can retrieve it
      const packet = packets.splice(0, 1)[0];
      if (packet.isSingleBytePacket(BL_PACKET_SYNC_OBSERVED_DATA0)) {
        Logger.success('Synced');
        return;
      }
      Logger.error('Wrong packet observed during sync sequence');
      process.exit(1);
    }

    if (timeWaited >= timeout) {
      Logger.error('Timed out waiting for sync sequence observed');
      process.exit(1);
    }
  }
  
}

// Do everything in an async function so we can have loops, awaits etc
const main = async () => {
  if (process.argv.length < 3) {
    console.log("usage: fw-updater <signed firmware>");
    process.exit(1);
  }
  const firmwareFilename = process.argv[2];

  // We need to know what's the length of the firmware that we're sending as an update.
  // It'll be passed to the target machine to make sure it has enough space for it.
  // XXXX what happened to .then(bin => bin.slice(BOOTLOADER_SIZE)); that was supposed to give us only the main application with the bootloader?

  Logger.info('Reading the firmware image...');
  const fwImage = await fs.readFile(path.join(process.cwd(), firmwareFilename));
  const fwLength = fwImage.length;
  Logger.success(`Read firmware image (${fwLength} bytes)`);

  Logger.info('Attempting to sync with the bootloader');
  await syncWithBootloader();
  Logger.success('Synced!');

  Logger.info('Requesting firmware update');
  const fwUpdatePacket = Packet.createSingleBytePacket(BL_PACKET_FW_UPDATE_REQ_DATA0);
  writePacket(fwUpdatePacket);
  await waitForSingleBytePacket(BL_PACKET_FW_UPDATE_RES_DATA0);
  Logger.success('Firmware update request accepted');

  Logger.info('Waiting for device ID request');
  await waitForSingleBytePacket(BL_PACKET_DEVICE_ID_REQ_DATA0);
  Logger.success('Device ID request recieved');

  // At this point we expect the bootloader to ask us for Device ID (to make sure they both match)

  const deviceId = fwImage[FWINFO_DEVICE_ID_OFFSET];
  const deviceIDPacket = new Packet(2, Buffer.from([BL_PACKET_DEVICE_ID_RES_DATA0, deviceId]));
  writePacket(deviceIDPacket);
  Logger.info(`Responding with device ID 0x${deviceId.toString(16)}`);

  Logger.info('Waiting for firmware length request');
  await waitForSingleBytePacket(BL_PACKET_FW_LENGTH_REQ_DATA0);
  Logger.success('Firmware length request recieved');

  const fwLengthPacketBuffer = Buffer.alloc(5);  // 5: 1 byte for the message kind, 4 bytes to store a little-endian uint32 value represnting the size
  fwLengthPacketBuffer[0] = BL_PACKET_FW_LENGTH_RES_DATA0;
  fwLengthPacketBuffer.writeUInt32LE(fwLength, 1);
  const fwLengthPacket = new Packet(5, fwLengthPacketBuffer);
  writePacket(fwLengthPacket);  // xxxx in episode 11 36:30 wrote .toBuffer()
  Logger.info('Responding with firmware length');

  // If that's unsuccessfull, meaning the firmware length is non-adequate, we'll get a NACK. 
  // If it's successfull, that's the moment the bootloader is going to start erasing its main app from flash
  // XXXX I SAW IT TAKES MINE MORE THAN HIS. Possible needs more delay time
  Logger.info('Waiting for a few seconds for main application to be erased...');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 1 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 2 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 3 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 4 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 5 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 6 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 7 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 8 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 9 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 10 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 11 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 12 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 13 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 14 sec)');
  await delay(1000);
  Logger.info('Waiting for a few seconds for main application to be erased... (waited 15 sec)');

  let bytesWritten = 0;
  while (bytesWritten < fwLength) {
    await waitForSingleBytePacket(BL_PACKET_READY_FOR_DATA_DATA0);

    const dataBytes = fwImage.subarray(bytesWritten, bytesWritten + PACKET_DATA_BYTES);
    //const dataBytes = fwImage.slice(bytesWritten, bytesWritten + PACKET_DATA_BYTES);  // Try to grab 16 bytes and send them out.
                                                                                      // Note: when we use slice(), if we try to slice more data than available,
                                                                                      // the operation doesn't fail, it gives back as many bytes as could be.
                                                                                      // This "edge case" will happen at the edge of the firmware image.
                                                                                      // XXXX slice deprecated?
    const dataLength = dataBytes.length;
    const dataPacket = new Packet(dataLength - 1, dataBytes); // The -1 is because we're ignoring the top 4 bits. Our packet length can be represented by 4 bits
    writePacket(dataPacket);  // xxxx in episode 11 45:19 wrote .toBuffer()
    bytesWritten += dataLength;

    Logger.info(`Wrote ${dataLength} bytes (${bytesWritten}/${fwLength})`);

    // Eventually, we should have written all of the bytes in the firmware image, or, will have timed out waiting for a packet, in which case we'll fail out
  }

  await waitForSingleBytePacket(BL_PACKET_UPDATE_SUCCESSFUL_DATA0);
  Logger.success("Firmware update complete!");
}

main()
  .finally(() => uart.close());
