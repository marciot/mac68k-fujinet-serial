/****************************************************************************
 *   mac68k-fuji-drivers (c) 2024 Marcio Teixeira                           *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/

/*
 * This is a virtual device that can be accessed from Mac OS.
 * It is activated by a special sequence of sector I/O that
 * selects a particular "magic" sector for subsequent I/O.
 *
 * The Pico disk interface should allow the virtual device
 * first dibs to handle any disk I/O prior to sending it to
 * the ESP32 for processing. This allows the virtual device
 * to interpret and respond to requests by the Mac FujiNet
 * serial drivers.
 *
 * See "not_mac_ndev_read" and "not_mac_ndev_write" for
 * an explanation of how the Pico disk I/O code does this.
 *
 */

/* The serial communication protocol between the Pico and the
 * ESP32 is defined as follows. All values are sent in network
 * order (big-endian); "Uxx" indicates unsigned, "Sxx" signed.
 *
 * I/O Length and Flags (FLG_LEN):
 *
 * The maximum payload size of a message is 500 bytes. Since
 * only 9 bits are needed to transmit the length, the most
 * significant seven bits are used as flags or reserved:
 *
 *           +--------------+--------------+----------------+
 *           | No. of bits  | Type [Mask]  | Description    |
 *           +--------------+--------------+----------------+
 *           | 1            | BIT [0x8000] | data request   |
 *           | 6            | BIT [0x7E00] | reserved       |
 *           | 9            | U9  [0x01FF] | length         |
 *           +--------------+--------------+--------------- +
 *
 * I/O Message:
 *
 * When the Pico has data to deliver to the ESP32, or wishes
 * to poll it for data, it will send the following message:
 *
 *           +---------------+--------------+---------------+
 *           | No. of bytes  | Type [Value] | Description   |
 *           +---------------+--------------+---------------+
 *           | 1             | CHAR ['S']   | command       |
 *           | 2             | FLG_LEN      | length        |
 *           | length        | U8           | payload       |
 *           +---------------+--------------+---------------+
 *
 * The "length" will be at most 500 bytes and can be zero if
 * no data is to be sent (when polling). If the "data request"
 * bit is set the ESP32 should reply in-turn with a message
 * containing up to 500 bytes of data:
 *
 *           +---------------+--------------+---------------+
 *           | No. of bytes  | Type [Value] | Description   |
 *           +---------------+--------------+---------------+
 *           | 2             | U16          | length        |
 *           | length        | U8           | payload       |
 *           +----------------------------------------------+
 *
 * The "length" portion of FLG_LEN can be 0 if no data is
 * waiting but cannot exceed 500. If "length" is exactly 500,
 * the Mac serial driver may assume more data is available and
 * repeat the request.
 *
 */

#pragma once

#include <ctype.h>

#define MAC_NDEV_LOOPBACK_TEST 1

#define MAC_NDEV_KNOCK_SEQ    {0,70,85,74,73}  // Macintosh -> FujiNet
#define MAC_NDEV_REQUEST_TAG  "NDEV"           // Macintosh -> FujiNet
#define MAC_NDEV_REPLY_TAG    "FUJI"           // FujiNet -> Macintosh
#define MAC_NDEV_HEADER_LEN   12
#define MAC_NDEV_NEGATIVE_LBA 0x007FFFFF

#define MAC_NDEV_ESP32_CMD    'S'

#define NELEMENTS(a) (sizeof(a)/sizeof(a[0]))
#define CHARS_TO_UINT16(a,b) ((((uint16_t)a) << 8) | ((uint16_t)b) )
#define UINT16_HI_BYTE(a) ((a & 0xFF00) >>  8)
#define UINT16_LO_BYTE(a)  (a & 0x00FF)

enum {
    MAC_NDEV_WAIT_KNOCK,
    MAC_NDEV_WAIT_MAGIC_WRITE,
    MAC_NDEV_WAIT_MAGIC_READ,
    MAC_NDEV_WAIT_MAGIC_SECTOR
} mac_ndev_state;

typedef enum {
    MAC_NDEV_READ,
    MAC_NDEV_WRITE
} mac_ndev_mode;

uint8_t  mac_ndev_knock = 0;
uint8_t  mac_ndev_drive;
uint32_t mac_ndev_sector;

void printHexDump(const uint8_t *ptr, uint16_t len) {
    short n = MIN(15, len);
    printf("MacNDev: '");
    for (int i = 0; i < n; i++) printf("%c"  , isprint(ptr[i]) ? ptr[i] : '.');
    printf("' ");
    for (int i = 0; i < n; i++) printf("%02x ", ptr[i]);
    printf("\n");
}

/* This function writes 12 bytes of header data that are used for
 * communications between the Mac and the Pico. Along with a maximum
 * payload size of 500, this fills a 512 byte block. It may also be
 * used as sector tags in certain points of the initial handshaking.
 * This header is not used for serial communications to the ESP32.
 */

void mac_ndev_put_header(uint8_t buff[], uint16_t len) {
    buff[ 0] = MAC_NDEV_REPLY_TAG[0];
    buff[ 1] = MAC_NDEV_REPLY_TAG[1];
    buff[ 2] = MAC_NDEV_REPLY_TAG[2];
    buff[ 3] = MAC_NDEV_REPLY_TAG[3];
    buff[ 4] = 0;
    buff[ 5] = 0;
    buff[ 6] = UINT16_HI_BYTE(len);
    buff[ 7] = UINT16_LO_BYTE(len);
    buff[ 8] = 0;
    buff[ 9] = 0;
    buff[10] = 0;
    buff[11] = 0;
}

/* This function reads the 12 bytes of header data that are used for
 * communications between the Mac and the Pico. Along with a maximum
 * payload size of 500, this fills a 512 byte block. It may also be
 * read from sector tags in certain points of the initial handshaking.
 * This header is not used for serial communications to the ESP32.
 */

bool mac_ndev_get_header(uint8_t buff[], uint16_t *len) {
    if (memcmp(buff, MAC_NDEV_REQUEST_TAG, 4)) {
        //printf("MacNDev: Invalid tag on I/O request: %4s\n", buff);
        return false;
    } else {
        *len = CHARS_TO_UINT16(buff[6], buff[7]);
        return true;
    }
}

/* This function is a state machine that follows a special sequence
 * of sector acccesses and returns "true" on the last block of the
 * sequence. It is used during handshaking to allow the Mac FujiNet
 * serial driver to announce its presence.
 */
bool mac_ndev_detect_knock_sequence(uint32_t sector) {
    const int mac_ndev_knock_sequence[5] = MAC_NDEV_KNOCK_SEQ;

    if (sector == mac_ndev_knock_sequence[mac_ndev_knock]) {
        printf("MacNDev: Got knock %d\n", mac_ndev_knock);
        if (++mac_ndev_knock == NELEMENTS(mac_ndev_knock_sequence)) {
            printf("MacNDev: Knock sequence complete!\n");
            mac_ndev_knock = 0;
            return true;
        }
    } else {
        mac_ndev_knock = 0;
    }
    return false;
}

/* This function processes reads and writes to the special magic sector.
 */
bool mac_ndev_magic_sector_io(uint8_t *tagPtr, uint8_t *blkPtr, mac_ndev_mode mode) {
    const uint16_t reqDat = 0x8000;
    uint16_t       len;

    #if MAC_NDEV_LOOPBACK_TEST
        static uint8_t  loopbackData[2000];
        static uint16_t loopbackLen = 0;
    #else
        static uint8_t ser_hdr[3] = {0};
    #endif

    if (mode == MAC_NDEV_READ) {
        #if MAC_NDEV_LOOPBACK_TEST
            const uint16_t dataToReturn = MIN(loopbackLen, 512 - MAC_NDEV_HEADER_LEN);
            memcpy (blkPtr + MAC_NDEV_HEADER_LEN, loopbackData, dataToReturn);
            memmove (loopbackData, loopbackData + dataToReturn, loopbackLen - dataToReturn);
            // Even though we are only returning dataToReturn bytes, we report back
            // on the total number of available bytes.
            mac_ndev_put_header (blkPtr, loopbackLen);
            printf("MacNDev: Got I/O read request (loopback len = %d)\n", loopbackLen);
            loopbackLen -= dataToReturn;
            printHexDump (blkPtr + MAC_NDEV_HEADER_LEN, dataToReturn);
        #else
            // Serial message header
            ser_hdr[0]  = MAC_NDEV_ESP32_CMD;  // 'S'
            ser_hdr[1]  = 0x80;                // Zero length + "request data"
            ser_hdr[2]  = 0x00;                // Zero length

            // Send request to the ESP32
            while (uart_is_readable(UART_ID))
                uart_getc (UART_ID);
            uart_write_blocking (UART_ID, ser_hdr, 3);

            // Receive reply from the ESP32
            uint8_t s[2];
            uart_read_blocking(UART_ID, s, 2);
            const short len = CHARS_TO_UINT16(s[0],s[1]);
            uart_read_blocking(UART_ID, blkPtr + MAC_NDEV_HEADER_LEN, len);
            // Append header to reply to Mac host
            mac_ndev_put_header (blkPtr, len);
            printf("MacNDev: Got I/O read request (len = %d)\n", len);
            //printHexDump (blkPtr, len + MAC_NDEV_HEADER_LEN);
        #endif
        return true;
    }
    else if (mode == MAC_NDEV_WRITE) {
        const bool headerInTags = mac_ndev_get_header(tagPtr, &len);
        if (headerInTags || mac_ndev_get_header(blkPtr, &len)) {
            const uint8_t *payload = blkPtr + (headerInTags ? 0 : MAC_NDEV_HEADER_LEN);
            if (!headerInTags) {
                tagPtr = blkPtr;
            }
            #if MAC_NDEV_LOOPBACK_TEST
                printf("MacNDev: Got I/O write request (len = %d, pend = %d)\n", len, loopbackLen);
                printHexDump (payload, len);
                if ((loopbackLen + len) <= NELEMENTS(loopbackData)) {
                    memcpy (loopbackData + loopbackLen, payload, len);
                    loopbackLen += len;
                } else {
                    printf("MacNDev: Overflow in loopback buffer!\n");
                }
            #else
                printf("MacNDev: Got I/O write request (len = %d)\n", len);
                // Serial message header
                ser_hdr[0] = MAC_NDEV_ESP32_CMD;  // 'S'
                ser_hdr[1] = tagPtr[6] & ~0x80;   // hi-byte of len (clear "request data")
                ser_hdr[2] = tagPtr[7];           // lo-byte of len
                // Send data to the ESP32
                while (uart_is_readable(UART_ID))
                    uart_getc(UART_ID);
                uart_write_blocking(UART_ID, ser_hdr, 3);
                uart_write_blocking(UART_ID, payload, len);
            #endif
            return true;
        } else {
            printf("\nMacNDev: Got write request to magic sector without tags: ");
            printHexDump (blkPtr, 512);
            return false;
        }
    }
    return false;
}

// This is a helper function called by "not_mac_ndev_read" and "not_mac_ndev_write"

bool is_mac_ndev_io (uint8_t drive, uint32_t sector, uint8_t *tagPtr, uint8_t *blkPtr, mac_ndev_mode mode) {
    //printf("MacNDev: drive %d; sector %ld; mode %d; state %d; knock %d; magic %d/%ld\n", drive, sector, mode, mac_ndev_state, mac_ndev_knock, mac_ndev_drive, mac_ndev_sector);

    if (sector == MAC_NDEV_NEGATIVE_LBA) {
        // If we get a negative LBA, it must be special I/O...
        printf("MacNDev: Got negative LBA!\n");
        mac_ndev_magic_sector_io (tagPtr, blkPtr, mode);
        if (mac_ndev_state != MAC_NDEV_WAIT_MAGIC_SECTOR) {
            // Finish partially complete handshake, as
            // the host is using negative LBA instead.
            mac_ndev_state = MAC_NDEV_WAIT_KNOCK;
        }
        return true;
    }

    // Listen for the knock sequence, which may be sent at any time
    // to start designated I/O sector selection.

    if (mac_ndev_detect_knock_sequence(sector)) {
        mac_ndev_state  = MAC_NDEV_WAIT_MAGIC_WRITE;
        mac_ndev_drive  = drive;
        mac_ndev_sector = 0;

        // When the knocking sequence is complete, send
        // back special tags to let the host know a
        // FujiNet device is present.

        mac_ndev_put_header(tagPtr, 0);
    }

    // Handle the current run state

    switch (mac_ndev_state) {
        case MAC_NDEV_WAIT_KNOCK:
            /* STEP 1: Device idle, waiting for a valid knock sequence.
             */
            //printf("MacNDev: waiting for knock\n");
            break;

        case MAC_NDEV_WAIT_MAGIC_WRITE:
            /* STEP 2: After knocking, the Mac will either do a negative LBA
             *         request, or write 512 bytes of magic data to a file.
             *         If we detect this, we save the sector number for
             *         subsequent I/O.
             */
            printf("MacNDev: waiting for magic write\n");
            if ((mode  == MAC_NDEV_WRITE) &&
                (drive == mac_ndev_drive)) {
                // Check whether the whole sector consists of
                // repetitions of the magic value.
                const char *magic = MAC_NDEV_REQUEST_TAG;
                for (int i = 0; i < 512; i++) {
                    const char expected = magic[i & 3];
                    const char received = blkPtr[i];
                    if (expected != received) {
                        printf("MacNDev: Magic sector rejected at byte %d, %c != %c\n", i, received, expected);
                        break;
                    }
                }
                // We've got a magic sector!
                mac_ndev_sector = sector;
                mac_ndev_state = MAC_NDEV_WAIT_MAGIC_READ;
                printf("MacNDev: Will use sector number %ld for I/O\n", mac_ndev_sector);
                return true;
            }
            break;

        case MAC_NDEV_WAIT_MAGIC_READ:
            /* STEP 3: The Mac client will now immediately read back
             *         from the file. We should return a special message
             *         with a tag and the logical block number. At this
             *         point, both the host and FujiNet have agreed on
             *         a special I/O block and handshaking is complete.
             */
            printf("MacNDev: waiting for magic read\n");
            if ((mode  == MAC_NDEV_READ) &&
                (drive == mac_ndev_drive) &&
                (sector == mac_ndev_sector)) {
                mac_ndev_put_header(tagPtr, 8);
                blkPtr[0] = MAC_NDEV_REPLY_TAG[0];
                blkPtr[1] = MAC_NDEV_REPLY_TAG[1];
                blkPtr[2] = MAC_NDEV_REPLY_TAG[2];
                blkPtr[3] = MAC_NDEV_REPLY_TAG[3];
                blkPtr[4] = (mac_ndev_sector & 0xFF000000) >> 24;
                blkPtr[5] = (mac_ndev_sector & 0x00FF0000) >> 16;
                blkPtr[6] = (mac_ndev_sector & 0x0000FF00) >>  8;
                blkPtr[7] = (mac_ndev_sector & 0x000000FF) >>  0;
                printf("MacNDev: Sent I/O sector to Mac host.\n");
                printf("MacNDev: Handshake complete.\n");
                mac_ndev_state = MAC_NDEV_WAIT_MAGIC_SECTOR;
                return true;
            }
            break;

        case MAC_NDEV_WAIT_MAGIC_SECTOR:
            /* STEP 4: We can now intercept all reads and writes to the
             *         magic sector as I/O.
             */
            if ((drive == mac_ndev_drive) && (sector == mac_ndev_sector)) {
                //printf("MacNDev: Magic sector access\n");
                return mac_ndev_magic_sector_io(tagPtr, blkPtr, mode);
            } else if (sector == mac_ndev_sector) {
                printf("MacNDev: Magic sector request to wrong drive? %d != %d\n", drive, mac_ndev_drive);
            }
            break;
        default:
            printf("MacNDev: Invalid state %d\n", mac_ndev_state);
    }
    return false;
}

/* Prior to reading data for a disk read from the ESP32, the Pico disk
 * I/O code should call "not_mac_ndev_read" to confirm the request is
 * regular disk I/O and not special I/O.
 *
 * Example:
 *
 *    if (not_mac_ndev_read (drive_num, block_num, tags_ptr, block_ptr)) {
 *        // Not special I/O, go ahead and fill the buffer with
 *        // with from the ESP32 as it is normal disk I/O
 *    }
 *
 * The arguments are as follows:
 *
 *    drive_num : A disk identifier
 *    block_num : A logical block address on disk
 *    tags_ptr  : Pointer to the 12 or 20 byte MacOS sector tags
 *    block_ptr : Pointer to the 512 byte block buffer
 *
 * If "not_mac_ndev_read" returns "false", the tags and block data will
 * have been filled with appropriate values to fullfill the request and
 * they should be sent to the Macintosh unmodified.
 *
 */
inline bool not_mac_ndev_read (uint8_t drive, uint32_t sector, uint8_t *tagPtr, uint8_t *blkPtr) {
    return !is_mac_ndev_io (drive, sector, tagPtr, blkPtr, MAC_NDEV_READ);
}

/* Prior to sending data for a disk write to the ESP32, the Pico disk
 * I/O code should call "not_mac_ndev_write" to confirm the request is
 * regular disk I/O and not special I/O.
 *
 * Example:
 *
 *    if (not_mac_ndev_write(drive_num, block_num, tags_ptr, block_ptr)) {
 *        // Not special I/O, pass the data on
 *        // to the ESP32 as normal disk write
 *    }
 *
 * If "not_mac_ndev_write" function returns "false", the write data
 * been processed as special I/O should not be sent to the ESP32 as
 * disk data.
 */
inline bool not_mac_ndev_write (uint8_t drive, uint32_t sector, uint8_t *tagPtr, uint8_t *blkPtr) {
    return !is_mac_ndev_io (drive, sector, tagPtr, blkPtr, MAC_NDEV_WRITE);
}

// Clean up

#undef MAC_NDEV_KNOCK_SEQ
#undef MAC_NDEV_REQUEST_TAG
#undef MAC_NDEV_REPLY_TAG
#undef MAC_NDEV_HEADER_LEN
#undef MAC_NDEV_NEGATIVE_LBA

#undef NELEMENTS
#undef CHARS_TO_UINT16
#undef UINT16_HI_BYTE
#undef UINT16_LO_BYTE
