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

#define BENCH_CHECK_MESSAGES 1
#define BENCH_CLEAR_MESSAGES 0
#define BENCH_SHOW_OPERATION 0
#define USE_ASCII_SEQUENCE   1

#include "FujiTests.h"
#include "FujiDebugMacros.h"
#include "FujiInterfaces.h"

#include <Serial.h>

#define kInputBufSIze 1024
#define kMesgBufSIze 2000

#if USE_ASCII_SEQUENCE
	static unsigned long nextRand (unsigned long seed) {
		if (seed < 'a') seed = 'a';
		seed++;
		if (seed == '{') seed = 'a';
		return seed;
	}
#else
	static unsigned long nextRand (unsigned long seed) {
		return seed * 214013 + 2531011;
	}
#endif

static OSErr flushSerialInput (short sInputRefNum) {
	unsigned char msg[kMesgBufSIze];
	ParamBlockRec pb;
	long availBytes;
	int i;
	for (i = 0; i < 4; i++) {
		for (;;) {
			OSErr err = SerGetBuf(sInputRefNum, &availBytes); CHECK_ERR;
			if (availBytes == 0) {
				break;
			}
			if (availBytes > kMesgBufSIze) {
				availBytes = kMesgBufSIze;
			}

			pb.ioParam.ioRefNum = sInputRefNum;
			pb.ioParam.ioBuffer  = (Ptr) msg;
			pb.ioParam.ioReqCount = availBytes;
			pb.ioParam.ioCompletion = 0;
			pb.ioParam.ioVRefNum = 0;
			pb.ioParam.ioPosMode = 0;
			err = PBRead(&pb, false); CHECK_ERR;
		}
		Delay (4,&availBytes);
	}
}

OSErr testSerialDriver() {
	SerShk mySerShkRec;
	short sInputRefNum, sOutputRefNum;
	long readCount;
	ParamBlockRec pb;
	Str255 myBuffer;
	Handle gInputBufHandle;
	OSErr err;
	unsigned char *msg = "\pThe Eagle has landed\r\n";

	// Open the serial drivers

	DEBUG_STAGE("Opening serial driver");

	err = OpenDriver("\p.AOut",  &sOutputRefNum); CHECK_ERR;
	err = OpenDriver("\p.AIn",   &sInputRefNum); CHECK_ERR;

	// Replace the default input buffer

	DEBUG_STAGE("Setting the buffer");

	gInputBufHandle = NewHandle(kInputBufSIze);
	HLock(gInputBufHandle);
	err = SerSetBuf(sInputRefNum, *gInputBufHandle, kInputBufSIze); CHECK_ERR;

	// Set the handshaking options

	DEBUG_STAGE("Setting the handshaking");

	mySerShkRec.fXOn = 0;
	mySerShkRec.fCTS = 0;
	mySerShkRec.errs = 0;
	mySerShkRec.evts = 0;
	mySerShkRec.fInX = 0;
	mySerShkRec.fDTR = 0;
	err = Control(sOutputRefNum, 14, &mySerShkRec); CHECK_ERR;

	// Configure the port

	DEBUG_STAGE("Configuring the baud");

	err = SerReset(sOutputRefNum, baud2400 + data8 + noParity + stop10); CHECK_ERR;

	DEBUG_STAGE("Flushing input data");
	flushSerialInput(sInputRefNum);

	DEBUG_STAGE("Sending a message");

	pb.ioParam.ioRefNum = sOutputRefNum;
	pb.ioParam.ioBuffer  = (Ptr) &msg[1];
	pb.ioParam.ioReqCount = msg[0];
	pb.ioParam.ioCompletion = 0;
	pb.ioParam.ioVRefNum = 0;
	pb.ioParam.ioPosMode = 0;
	err = PBWrite(&pb, false); CHECK_ERR;

	// Receive a message

	DEBUG_STAGE("Checking bytes available");

	err = SerGetBuf(sInputRefNum, &readCount); CHECK_ERR;

	printf("Bytes avail %ld\n", readCount);

	if (readCount > 0) {
		if (readCount > 255) {
			readCount = 255;
		}

		DEBUG_STAGE("Reading bytes");

		myBuffer[0] = readCount;

		// Read a message
		pb.ioParam.ioRefNum = sInputRefNum;
		pb.ioParam.ioBuffer  = (Ptr) &myBuffer[1];
		pb.ioParam.ioReqCount = readCount;
		pb.ioParam.ioCompletion = 0;
		pb.ioParam.ioVRefNum = 0;
		pb.ioParam.ioPosMode = 0;
		err = PBRead(&pb, false); CHECK_ERR;

		printf("%#s\n", myBuffer);
	}

	DEBUG_STAGE("Restoring buffer");

	err = SerSetBuf(sInputRefNum, *gInputBufHandle, 0); CHECK_ERR;
	DisposeHandle(gInputBufHandle);

	// Close Serial port
	DEBUG_STAGE("Killing IO");

	err = KillIO(sOutputRefNum); CHECK_ERR;

	DEBUG_STAGE("Closing driver");

	err = CloseDriver(sInputRefNum); CHECK_ERR;
	err = CloseDriver(sOutputRefNum); CHECK_ERR;
}

OSErr testSerialThroughput(Boolean useSerGet) {
	long bytesRead, bytesWritten, availBytes, startTicks, endTicks;
	unsigned long writeRand, readRand;
	short sInputRefNum, sOutputRefNum, i, j;
	ParamBlockRec pb;
	Handle gInputBufHandle;
	OSErr err;
	unsigned char msg[kMesgBufSIze];

	// Open the serial drivers

	DEBUG_STAGE("Opening serial driver");

	err = OpenDriver("\p.AOut",  &sOutputRefNum); CHECK_ERR;
	err = OpenDriver("\p.AIn",   &sInputRefNum); CHECK_ERR;

	// Replace the default input buffer

	DEBUG_STAGE("Setting the buffer");

	gInputBufHandle = NewHandle(kInputBufSIze);
	HLock(gInputBufHandle);
	SerSetBuf(sInputRefNum, *gInputBufHandle, kInputBufSIze); CHECK_ERR;

	DEBUG_STAGE("Flushing input data");
	flushSerialInput(sInputRefNum);

	DEBUG_STAGE("Testing serial throughput");

	for (i = 0; i < 10; i++) {
		const short messageSize = (3 << i) >> 1;
		char lastOp;

		bytesRead = bytesWritten = 0;
		writeRand = readRand = 0;
		startTicks = endTicks = Ticks;

		for (;;) {
			// Send data for 20 seconds

			if (endTicks - startTicks < 1200) {
				endTicks = Ticks;

				// Fill the message with pseudo-random data

				#if BENCH_CLEAR_MESSAGES
					memset(msg, '&', kMesgBufSIze);
				#endif

				#if BENCH_CHECK_MESSAGES
					for (j = 0; j < messageSize; j++) {
						writeRand = nextRand (writeRand);
						msg[j] = writeRand & 0xFF;
					}
				#endif

				#if BENCH_SHOW_OPERATION
					// Send a message
					if (lastOp != 'W') {
						printf ("W\r");
						lastOp = 'W';
					}
				#endif

				pb.ioParam.ioRefNum = sOutputRefNum;
				pb.ioParam.ioBuffer  = (Ptr)msg;
				pb.ioParam.ioReqCount = messageSize;
				pb.ioParam.ioActCount = 1; // Test whether the driver needs to clear this
				pb.ioParam.ioCompletion = 0;
				pb.ioParam.ioVRefNum = 0;
				pb.ioParam.ioPosMode = 0;
				err = PBWrite(&pb, false); CHECK_ERR;
				bytesWritten += pb.ioParam.ioActCount;

				#if BENCH_CHECK_MESSAGES
					if (pb.ioParam.ioReqCount != messageSize) {
						printf("ioReqCount changed after write! %ld != %d\n", pb.ioParam.ioReqCount, messageSize);
					}

					if (pb.ioParam.ioActCount != messageSize) {
						printf("ioActCount not correct after write! %ld != %d\n", pb.ioParam.ioActCount, messageSize);
					}
				#endif
			}

			// Keep reading data until we got back all the data we wrote

			if (bytesRead != bytesWritten) {
				// Receive a message

				if (useSerGet) {
					err = SerGetBuf(sInputRefNum, &availBytes); CHECK_ERR;
				} else {
					availBytes = bytesWritten - bytesRead;
				}

				if (availBytes < 0) {
					printf("Got negative avail bytes! %ld\n", availBytes);
				}

				if (availBytes > kMesgBufSIze) {
					availBytes = kMesgBufSIze;
				}

				if (availBytes) {
					#if BENCH_SHOW_OPERATION
						if (lastOp != 'R') {
							printf ("R\r");
							lastOp = 'R';
						}
					#endif

					#if BENCH_CLEAR_MESSAGES
						memset(msg, '#', kMesgBufSIze);
					#endif

					// Read a message
					pb.ioParam.ioRefNum = sInputRefNum;
					pb.ioParam.ioBuffer  = (Ptr) msg;
					pb.ioParam.ioReqCount = availBytes;
					pb.ioParam.ioActCount = 10; // Test whether the driver needs to clear this
					pb.ioParam.ioCompletion = 0;
					pb.ioParam.ioVRefNum = 0;
					pb.ioParam.ioPosMode = 0;
					err = PBRead(&pb, false); CHECK_ERR;

					#if BENCH_CHECK_MESSAGES
						if (pb.ioParam.ioReqCount != availBytes) {
							printf("ioReqCount changed after read! %ld != %d\n", pb.ioParam.ioReqCount, availBytes);
						}

						if (pb.ioParam.ioActCount != availBytes) {
							printf("ioActCount not correct after read! %ld != %d\n", pb.ioParam.ioActCount, availBytes);
						}

						// Verify the message against the pseudo-random data

						for (j = 0; j < pb.ioParam.ioActCount; j++) {
							unsigned char expected;
							readRand = nextRand (readRand);
							expected = readRand & 0xFF;
							if (msg[j] != expected) {
								printf("Data verification error on byte %ld: %x != %x\n", bytesRead + j, msg[j] & 0xFF, expected);
								printHexDump (msg, j, pb.ioParam.ioActCount);
								goto error;
							}
						}
					#endif

					bytesRead += pb.ioParam.ioActCount;
				} // availBytes
			} // bytesRead != bytesWritten
			else {
				break;
			}
		}
		endTicks = Ticks;

		printf("%3d byte messages: out: %6ld ; in %6ld ... ", messageSize, bytesWritten, bytesRead);
		printThroughput (bytesRead + bytesWritten, endTicks - startTicks);
	}

error:
	SerSetBuf(sInputRefNum, *gInputBufHandle, 0); CHECK_ERR;
	DisposeHandle(gInputBufHandle);

	KillIO(sOutputRefNum);
	CloseDriver(sInputRefNum);
	CloseDriver(sOutputRefNum);
}