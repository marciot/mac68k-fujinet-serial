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

#include <Disks.h>

#include <string.h>

#include "FujiTests.h"
#include "FujiDebugMacros.h"
#include "FujiInterfaces.h"

short chosenDriveNum;
short chosenDrvrRefNum;

static OSErr findDrive(short drive) {
	OSErr err;
	DrvQElPtr qe;
	const QHdrPtr qh = GetDrvQHdr();
	for(qe = (DrvQElPtr) qh->qHead; qe; qe = (DrvQElPtr) qe->qLink) {
		if (qe->dQDrive == drive) {
			chosenDriveNum   = qe->dQDrive;
			chosenDrvrRefNum = qe->dQRefNum;
			return noErr;
		}
	}
	err = -1;
	printf("Can't find drive\n");
	return err;
}

OSErr chooseDrive() {
	short drive;
	printf("Please select drive: ");
	scanf("%d", &drive);
	findDrive(drive);
}

OSErr testFloppyLoopback() {
	ParamBlockRec pb;
	FujiSerDataHndl data;
	OSErr err;
	const short messageSize = 512;
	char msg[512];

	DEBUG_STAGE("Getting FujiNet handle");

	data = getFujiSerialDataHndl ();
	if (data && *data && (*data)->conn.iopb.ioPosOffset) {

		pb.ioParam.ioRefNum     = (*data)->conn.iopb.ioRefNum;
		pb.ioParam.ioPosMode    = fsFromStart;
		pb.ioParam.ioPosOffset  = (*data)->conn.iopb.ioPosOffset;
		pb.ioParam.ioVRefNum    = (*data)->conn.iopb.ioVRefNum;
		pb.ioParam.ioBuffer     = (Ptr)msg;
		pb.ioParam.ioReqCount   = 512;
		pb.ioParam.ioCompletion = 0;

		printf("Driver ref number     %d\n", pb.ioParam.ioRefNum);
		printf("Drive number:         %d\n", pb.ioParam.ioVRefNum);
		printf("Magic sector:         %ld\n",pb.ioParam.ioPosOffset / 512);

		DEBUG_STAGE("Writing block");

		FUJI_TAG_ID  = MAC_FUJI_REQUEST_TAG;
		FUJI_TAG_SRC = 0;
		FUJI_TAG_LEN = messageSize;

		err = PBWriteSync(&pb); CHECK_ERR;

		DEBUG_STAGE("Reading block");

		err = PBReadSync(&pb);  CHECK_ERR;
	} else {
		DEBUG_STAGE("Unable to get FujiNet handle");
	}
}

OSErr testFloppyThroughput() {
	ParamBlockRec pb;
	FujiSerDataHndl data;
	OSErr err;
	const short messageSize = 512;
	long bytesRead = 0, bytesWritten = 0;
	long startTicks, endTicks;
	char msg[512];

	DEBUG_STAGE("Getting FujiNet handle");

	data = getFujiSerialDataHndl ();
	if (data && *data && (*data)->conn.iopb.ioPosOffset) {

		pb.ioParam.ioRefNum     = (*data)->conn.iopb.ioRefNum;
		pb.ioParam.ioPosMode    = fsFromStart;
		pb.ioParam.ioPosOffset  = (*data)->conn.iopb.ioPosOffset;
		pb.ioParam.ioVRefNum    = (*data)->conn.iopb.ioVRefNum;
		pb.ioParam.ioBuffer     = (Ptr)msg;
		pb.ioParam.ioReqCount   = 512;
		pb.ioParam.ioCompletion = 0;

		printf("Driver ref number     %d\n", pb.ioParam.ioRefNum);
		printf("Drive number:         %d\n", pb.ioParam.ioVRefNum);
		printf("Magic sector:         %ld\n",pb.ioParam.ioPosOffset / 512);

		DEBUG_STAGE("Testing floppy throughput...\n");

		for (startTicks = Ticks; Ticks - startTicks < 1200;) {
			FUJI_TAG_ID  = MAC_FUJI_REQUEST_TAG;
			FUJI_TAG_SRC = 0;
			FUJI_TAG_LEN = messageSize;

			err = PBWriteSync(&pb); CHECK_ERR;
			bytesWritten += pb.ioParam.ioActCount;

			err = PBReadSync(&pb);  CHECK_ERR;
			bytesRead += pb.ioParam.ioActCount;

		}
		endTicks = Ticks;
		printf(" out: %6ld ; in %6ld ... ", bytesWritten, bytesRead);
		printThroughput (bytesRead + bytesWritten, endTicks - startTicks);

	} else {
		DEBUG_STAGE("Unable to get FujiNet handle");
	}
}

OSErr readSectorAndTags() {
	ParamBlockRec pb;
	OSErr         err;
	TagBuffer     tag;
	SectorBuffer  sector;
	int           i;
	int           sectorNum;

	printf("Please type in sector: ");
	scanf("%d", &sectorNum);

	memset(tag.bytes,    0xAA, sizeof(tag.bytes));
	memset(sector.bytes, 0xAA, sizeof(sector.bytes));

	pb.ioParam.ioRefNum     = chosenDrvrRefNum;
	pb.ioParam.ioCompletion = 0;
	pb.ioParam.ioBuffer     = sector.bytes;
	pb.ioParam.ioReqCount   = 512;
	pb.ioParam.ioPosMode    = fsFromStart;
	pb.ioParam.ioPosOffset  = sectorNum * 512;
	pb.ioParam.ioVRefNum    = chosenDriveNum;

	printf("Setting tag buffer\n");

	err = SetTagBuffer(tag.bytes); CHECK_ERR;

	printf("Calling .Sony driver with offset of %d\n", sectorNum * 512);

	err = PBReadSync(&pb); CHECK_ERR;

	SetTagBuffer(NULL);

	printf("All values initialized to AA prior to read.\n");

	printf("Block (initialized to AA): ");
	for(i = 0; i < 20; i++) {
		printf("%02x ", (unsigned char)sector.bytes[i]);
		if (i % 24 == 0) {
			printf("\n");
		}
	}
	printf("\n");

	printf("Sector Tags (initialized to AA):\n");
	for(i = 0; i < NELEMENTS(tag.bytes); i++) {
		printf("%02x ", (unsigned char)tag.bytes[i]);
	}
	printf("\n");

	return err;
}