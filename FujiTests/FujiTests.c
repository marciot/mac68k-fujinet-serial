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

#include <stdio.h>
#include <ctype.h>

#include <Files.h>
#include <Disks.h>
#include <Devices.h>
#include <Resources.h>
#include <Retrace.h>

#include "FujiNet.h"
#include "FujiDebugMacros.h"
#include "FujiInterfaces.h"

#include "FujiTests.h"

char *errorStr(OSErr err);

void printHexDump (const unsigned char *ptr, short at, unsigned short len) {
	short i, n;
	if (at) {
		at =  MAX (0, at - 5);
		ptr += at;
		len -= at;
	}
	n = MIN (16, len);
	printf("'");
	for (i = 0; i < n; i++) printf("%c"  , isprint (ptr[i]) ? ptr[i] : '.');
	printf("' ");
	for (i = 0; i < n; i++) printf("%02x ", ptr[i] & 0xFF);
	printf("\n");
}

static OSErr printDriveVolumes(int driveNum) {
	VCB *qe;
	const QHdrPtr qh = GetVCBQHdr();
	for(qe = (VCB*) qh->qHead; qe; qe = (VCB*) qe->qLink) {
		if (driveNum == qe->vcbDrvNum)
		printf(" %#27.27s ", qe->vcbVN);
	}
	return noErr;
}

static OSErr printDriveQueue() {
	DrvQElPtr qe;
	const QHdrPtr qh = GetDrvQHdr();
	for(qe = (DrvQElPtr) qh->qHead; qe; qe = (DrvQElPtr) qe->qLink) {
		size_t size = (size_t)(qe->dQDrvSz) | ((qe->qType == 1) ? (size_t)(qe->dQDrvSz2) << 16 : 0);
		printf("\n%4d: [%7.2f MBs]  ", qe->dQDrive, (float)(size)/2/1024);
		printDriveVolumes(qe->dQDrive);
	}
	printf("\n");
	return noErr;
}

static OSErr openFujiNet() {
	const short BootDrive = *((short *)0x210); // BootDrive low-memory global
	OSErr err = fujiSerialOpen (BootDrive); CHECK_ERR;
	return err;
}

static OSErr printUnitTable() {
	short i, lines = 0;
	Handle *table = (Handle*) UTableBase;
	DCtlEntry *dce;
	for(i = 0; i < UnitNtryCnt; i++) {
		if (table[i]) {
			unsigned long drvrSize = 0, dataSize = 0;
			char drvrZone = '-', dataZone = '-';
			Boolean dRamBased;
			DRVRHeader *header;
			//char dceState = (HGetState (table[i]) & 0x80) ? 'L' : 'u';
			char dceState = ' ';

			dce = (DCtlEntry*) *table[i];

			dRamBased    = dce->dCtlFlags & dRAMBasedMask;
			header       = (DRVRHeader*) (dRamBased ? *(Handle)dce->dCtlDriver : dce->dCtlDriver);

			if (dRamBased) {
				Handle drvrHand = (Handle)dce->dCtlDriver;
				drvrSize = GetHandleSize(drvrHand);
				drvrZone = (HandleZone(drvrHand) == SystemZone()) ? 's' : 'a';
				if (dce->dCtlStorage) {
					Handle dataHand = (Handle)dce->dCtlStorage;
					dataSize = GetHandleSize(dataHand);
					dataZone = (HandleZone(dataHand) == SystemZone()) ? 's' : 'a';
				}
			}

			printf("\n%4d: %3d %#10.10s %c%s %s %s %c%c%c%c%c%c %c%c%c%c%c%c %3ld %3ld %c%c", i, dce->dCtlRefNum, header->drvrName,
				dceState,
				(dce->dCtlFlags & dRAMBasedMask)   ? "    RAM" : "    ROM",
				(dce->dCtlFlags & dOpenedMask)     ? "    open" : "  closed",
				(dce->dCtlFlags & drvrActiveMask)  ? "  active" : "inactive",

				(dce->dCtlFlags & dNeedLockMask)   ? 'L' : '-',
				(dce->dCtlFlags & dNeedTimeMask)   ? 'T' : '-',
				(dce->dCtlFlags & dStatEnableMask) ? 'S' : '-',
				(dce->dCtlFlags & dCtlEnableMask)  ? 'C' : '-',
				(dce->dCtlFlags & dWritEnableMask) ? 'W' : '-',
				(dce->dCtlFlags & dReadEnableMask) ? 'R' : '-',

				(header->drvrFlags & dNeedLockMask)    ? 'L' : '-',
				(header->drvrFlags & dNeedTimeMask)    ? 'T' : '-',
				(header->drvrFlags & dStatEnableMask)  ? 'S' : '-',
				(header->drvrFlags & dCtlEnableMask)   ? 'C' : '-',
				(header->drvrFlags & dWritEnableMask)  ? 'W' : '-',
				(header->drvrFlags & dReadEnableMask)  ? 'R' : '-',
				drvrSize, dataSize, drvrZone, dataZone
			);
			if (lines++ % 22 == 0) {
				printf("\n\n==== MORE ===="); getchar();
			}
		}
	}
}

static OSErr printDriverStatus() {
	unsigned long bytesRead, bytesWritten;

	printf("\n");
	printf("Fuji status:          %s\n", isFujiConnected()       ? "connected" : "not connected");
	printf("Modem driver:         %s\n", isFujiModemRedirected() ? "installed" : "not installed");
	printf("Printer driver:       %s\n\n", isFujiPrinterRedirected() ? "installed" : "not installed");

	if (fujiSerialStats (&bytesRead, &bytesWritten)) {
		FujiSerDataHndl data = getFujiSerialDataHndl ();
		if (data) {
			printf("Driver ref number     %d\n", (*data)->conn.iopb.ioRefNum);
			printf("Drive number:         %d\n", (*data)->conn.iopb.ioVRefNum);
			printf("Magic sector:         %ld\n", (*data)->conn.iopb.ioPosOffset / 512);
		}

		printf("Total bytes read:     %ld\n", bytesRead);
		printf("Total bytes written:  %ld\n", bytesWritten);
	} else {
		printf("Cannot get status\n");
	}
}

static OSErr setVBLFrequency() {
	if (isFujiModemRedirected()) {
		OSErr err;
		short sInputRefNum, sOutputRefNum;
		FujiSerDataHndl data = getFujiSerialDataHndl ();

		err = OpenDriver("\p.AOut",  &sOutputRefNum); CHECK_ERR;
		err = OpenDriver("\p.AIn",   &sInputRefNum); CHECK_ERR;

		if (data) {
			short count;
			printf("Current VBL interval: %d\n", (*data)->vblCount);
			printf("Please enter new VBL interval (1-255): ");
			scanf("%d", &count);
			(*data)->vblCount = count;
		}

		CloseDriver(sInputRefNum);
		CloseDriver(sOutputRefNum);
	} else {
		printf("Please connect to the FujiNet and redirect the serial port first\n");
	}
}

static OSErr testFujiWrite() {
	short sFujiRefNum;
	ParamBlockRec pb;
	OSErr err;
	unsigned char *msg = "\pThis is a test\r\n";

	// Open the serial drivers

	DEBUG_STAGE("Opening Fuji driver");

	err = OpenDriver("\p.Fuji",  &sFujiRefNum); CHECK_ERR;

	DEBUG_STAGE("Sending a message");

	pb.ioParam.ioRefNum = sFujiRefNum;
	pb.ioParam.ioBuffer  = (Ptr) &msg[1];
	pb.ioParam.ioReqCount = msg[0];
	pb.ioParam.ioCompletion = 0;
	pb.ioParam.ioVRefNum = 0;
	pb.ioParam.ioPosMode = 0;
	err = PBWrite(&pb, false); CHECK_ERR;

	//DEBUG_STAGE("Closing driver");

	//CloseDriver(sFujiRefNum);
}

void printThroughput(long bytesTransfered, long timeElapsed) {
	long bytesPerSecond = (timeElapsed == 0) ? 0 : (bytesTransfered * 60 / timeElapsed);
	if (bytesPerSecond > 1024) {
		printf( "   %3ld Kbytes/sec\n", bytesPerSecond / 1024);
	} else {
		printf( "   %3ld bytes/sec\n", bytesPerSecond);
	}
}

char *errorStr(OSErr err) {
	switch(err) {
		case controlErr:   return "Driver can't respond to control calls";   // -17
		case readErr:      return "Driver can't respond to read calls";      // -19
		case writErr:      return "Driver can't respond to write calls";     // -20
		case notOpenErr:   return "Driver isn't open";                       // -28
		case eofErr:       return "End of file";                             // -39
		case nsDrvErr:     return "No such drive";
		case fnfErr:       return "File not found error";                    // -43
		case dupFNErr:     return "File already exists";                     // -48
		case opWrErr:      return "File already open with write permission"; // -49
		case paramErr:     return "Error in user param list";                // -50
		case rfNumErr:     return "Ref num error";                           // -51
		case nsvErr:       return "No such volume";                          // -56
		case noDriveErr:   return "Drive not installed";                     // -64
		case offLinErr:    return "Read/write requested for offline drive";  // -65
		case sectNFErr:    return "Sector number never found on a track";    // -81
		case portInUse:    return "Port in use";                             // -97
		case portNotCf:    return "Port not configured";                     // -98
		case resNotFound:  return "Resource not found";                      // -192
		default:           return "";
	}
}

static OSErr printOwnedResourceId() {
	short unitNumber, subId, resId;
	printf("Please select driver: ");
	scanf("%d", &unitNumber);
	printf("Enter resource sub id: ");
	scanf("%d", &subId);
	resId = 0xC000 | (unitNumber << 5) | subId;
	printf("Owned resource id: %d\n", resId);
}

static OSErr mainHelp() {
	printf("1: Drive tests\n");
	printf("2: FujiNet interface tests\n");
	printf("3: Serial driver tests\n");
	printf("4: Miscellaneous tests\n");
	printf("q: Exit\n");
	return noErr;
}

static OSErr diskHelp() {
	printf("1: List drives (and mounted volumes)\n");
	printf("2: Select drive\n");
	printf("3: Read sector and tags\n");
	printf("q: Main menu\n");
	return noErr;
}

static OSErr diskChoice(char mode) {
	switch(mode) {
		case '1': printDriveQueue(); break;
		case '2': chooseDrive(); break;
		case '3': readSectorAndTags(); break;
		default: -1;
	}
	return noErr;
}

static OSErr drvrHelp() {
	printf("1: Print unit table\n");
	printf("2: Print status of drivers\n");
	printf("3: Install modem driver\n");
	printf("4: Install printer driver\n");
	printf("5: Test serial driver\n");
	printf("6: Test serial throughput with blocking I/O\n");
	printf("7: Test serial throughput with non-blocking I/O\n");
	printf("8: Set VBL frequency\n");
	printf("q: Main menu\n");
	return noErr;
}

static OSErr drvrChoice(char mode) {
	switch(mode) {
		case '1': printUnitTable(); break;
		case '2': printDriverStatus(); break;
		case '3': fujiSerialRedirectModem(); break;
		case '4': fujiSerialRedirectPrinter(); break;
		case '5': testSerialDriver(); break;
		case '6': testSerialThroughput (false); break;
		case '7': testSerialThroughput (true); break;
		case '8': setVBLFrequency(); break;
		default: -1;
	}
	return noErr;
}

static OSErr miscHelp() {
	printf("1: Compute owned resource id\n");
	printf("q: Main menu\n");
	return noErr;
}

static OSErr miscChoice(char mode) {
	switch(mode) {
		case '1': printOwnedResourceId(); break;
		default: -1;
	}
	return noErr;
}

static OSErr fujiHelp() {
	printf("1: Open FujiNet device\n");
	printf("2: Test Fuji direct write\n");
	printf("3: Test floppy port read/write\n");
	printf("4: Test floppy port throughput\n");
	printf("q: Main menu\n");
	return noErr;
}

static OSErr fujiChoice(char mode) {
	switch(mode) {
		case '1': openFujiNet(); break;
		case '2': testFujiWrite(); break;
		case '3': testFloppyLoopback(); break;
		case '4': testFloppyThroughput(); break;
		default: -1;
	}
	return noErr;
}

static OSErr mtcpHelp() {
	printf("1: Basic MacTCP test\n");
	printf("q: Main menu\n");
	return noErr;
}

static OSErr mtcpChoice(char mode) {
	switch(mode) {
		case '1': testBasicTCP(); break;
		default: -1;
	}
	return noErr;
}

int main() {
	OSErr   err;
	short   command, inOut = 100;
	char    buf[100], c = 0, mode = 0;

	printf("built " __DATE__ " " __TIME__ "\n\n\n");

	while (c != 'q') {
		switch(mode) {
			case '1': diskHelp(); break;
			case '2': fujiHelp(); break;
			case '3': drvrHelp(); break;
			case '4': mtcpHelp(); break;
			case '5': miscHelp(); break;
			default:  mainHelp();
		}

		printf(">");
		c = getchar();
		while (isspace(c)) {
			c = getchar();
		}

		if (mode && (c == 'q')) {
			mode = 0;
			c = ' ';
		} else {
			switch(mode) {
				case '1': err = diskChoice(c); break;
				case '2': err = fujiChoice(c); break;
				case '3': err = drvrChoice(c); break;
				case '4': err = mtcpChoice(c); break;
				case '5': err = miscChoice(c); break;
				default: mode = c;
			}
		}
		if (err == -1) {
			printf("Invalid choice!\n");
		}
		printf("\n\n");
	}

	return 0;
}