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

#include "FujiNet.h"
#include "FujiDebugMacros.h"
#include "FujiInterfaces.h"

// Reference: Macintosh Tech Notes: #272: What Your Sony Drives For You, April 1990

enum {
    sonyEnableCache  = 0xFF00,
    sonyDisableCache = 0x0000,
    sonyRemoveCache  = 0x00FF,
    sonyInstallCache = 0x0001
};

static OSErr sonyTrackCacheControl(short drive, short drvrRefNum, short op) {
    ParamBlockRec pb;
    pb.cntrlParam.ioCRefNum    = drvrRefNum;
    pb.cntrlParam.ioCompletion = 0;
    pb.cntrlParam.csCode       = 9;
    pb.cntrlParam.csParam[0]   = op;
    pb.cntrlParam.ioVRefNum    = 0;
    return PBControlSync(&pb);
}

static OSErr sonySetTagBuffer(short drive, short drvrRefNum, Ptr tagBuffer) {
    ParamBlockRec pb;
    pb.cntrlParam.ioCRefNum    = drvrRefNum;
    pb.cntrlParam.ioCompletion = 0;
    pb.cntrlParam.csCode       = 8;
    ((Ptr*)pb.cntrlParam.csParam)[0] = tagBuffer;
    pb.cntrlParam.ioVRefNum    = 0;
    return PBControlSync(&pb);
}

static OSErr getDriveAndDrvr(short vRefNum, short *drive, short *drvrRefNum) {
    const short FSFCBLen  = *((short *)0x3F6); // FSFCBLen low-memory global
    OSErr err;

    // Use FSFCBLen to check for HFS, according to Inside Macintosh IV-97

    if (FSFCBLen > 0) {
        // If FSFCBLen is positive, BootDrive is a working directory
        // reference number

        HParamBlockRec paramBlock;
        paramBlock.volumeParam.ioCompletion = 0;
        paramBlock.volumeParam.ioNamePtr = NULL;
        paramBlock.volumeParam.ioVRefNum = vRefNum;
        paramBlock.volumeParam.ioVolIndex = 0;
        err = PBHGetVInfo(&paramBlock, false); CHECK_ERR;
        *drive  = paramBlock.volumeParam.ioVDrvInfo;
        *drvrRefNum = paramBlock.volumeParam.ioVDRefNum;
        return noErr;
    }
    else if (vRefNum > 0) {
        // If vRefNum is positive, it must be a drive number

        DrvQElPtr qe;
        const QHdrPtr qh = GetDrvQHdr();
        for(qe = (DrvQElPtr) qh->qHead; qe; qe = (DrvQElPtr) qe->qLink) {
            if (qe->dQDrive == vRefNum) {
                *drive  = qe->dQDrive;
                *drvrRefNum = qe->dQRefNum;
                return noErr;
            }
        }
        err = -1;
    }
    else {
        // If vRefNum is negative, it must be a volume reference number

        VCB *qe;
        const QHdrPtr qh = GetVCBQHdr();
        for(qe = (VCB*) qh->qHead; qe; qe = (VCB*) qe->qLink) {
            if (qe->vcbVRefNum == vRefNum) {
                *drive  = qe->vcbDrvNum;
                *drvrRefNum = qe->vcbDRefNum;
                return noErr;
            }
        }
        err = -1;
    }
    #if DEBUG
        printf("Can't find drive\n");
    #endif
    return err;
}

OSErr fujiInit (struct FujiConData *fuji) {
    fuji->fRefNum = 0;
}

Boolean fujiReady (struct FujiConData *fuji) {
    return fuji->iopb.ioRefNum != 0;
}

OSErr fujiOpen (struct FujiConData *fuji, short vRefNum) {
    long          inOutCount;
    SectorBuffer  sector;
    ParamBlockRec pb;
    short         i, drvrRefNum, driveNum;
    long          sectorAddr;
    const char    knockSeq[] = MAC_FUJI_KNOCK_SEQ;

    OSErr err = getDriveAndDrvr (vRefNum, &driveNum, &drvrRefNum); CHECK_ERR;

    // Create and open the special FujiNet file

    DEBUG_STAGE("Creating file");

    err = Create(MAC_FUJI_NDEV_FILE, driveNum, MAC_FUJI_CREATOR, MAC_FUJI_TYPE);
    if (err != dupFNErr) CHECK_ERR;

    DEBUG_STAGE("Opening file");

    // For some reason, FSOpen seems to crash on System 1.0, use PBOpen instead

    pb.ioParam.ioCompletion = NULL;
    pb.ioParam.ioNamePtr    = MAC_FUJI_NDEV_FILE;
    pb.ioParam.ioVRefNum    = driveNum;
    pb.ioParam.ioVersNum    = 0;
    pb.ioParam.ioPermssn    = 2;
    pb.ioParam.ioMisc       = NULL;
    err = PBOpenSync(&pb); CHECK_ERR;
    fuji->fRefNum = pb.ioParam.ioRefNum;

    DEBUG_STAGE("Disabling Cache");

    err = sonyTrackCacheControl(driveNum, drvrRefNum, sonyDisableCache | sonyRemoveCache); ON_ERROR();

    // Send knocking sequence

    DEBUG_STAGE("Knocking");

    for (i = 0; (i < NELEMENTS(knockSeq)) && (err == noErr); i++) {
        SectorBuffer  sector;
        pb.ioParam.ioRefNum     = drvrRefNum;
        pb.ioParam.ioCompletion = 0;
        pb.ioParam.ioBuffer     = sector.bytes;
        pb.ioParam.ioReqCount   = 512;
        pb.ioParam.ioPosMode    = fsFromStart;
        pb.ioParam.ioPosOffset  = 512L * (long) knockSeq[i];
        pb.ioParam.ioVRefNum    = driveNum;
        err = PBReadSync(&pb); CHECK_ERR;
    }

    // Did we get a FujiNet reply?

    #if DEBUG
        if (BufTgFNum == MAC_FUJI_REPLY_TAG) {
            printf("FujiNet device present!\n");
        } else {
            printf("FujiNet device not detected.\n");
        }

        printf("BufTgFNum:   %lx\n", BufTgFNum);
        printf("BufTgFFlag:  %x\n",  BufTgFFlag);
        printf("BufTgFBkNum: %x\n",  BufTgFBkNum);
        printf("BufTgDate:   %lx\n", BufTgDate);
    #endif

    if (BufTgFNum != MAC_FUJI_REPLY_TAG) {
        err = -1;
        goto cleanup;
    }

    // Fill buffer with special bytes

    DEBUG_STAGE("Clearing buff");

    for (inOutCount = 0; inOutCount < NELEMENTS(sector.values); inOutCount++) {
        sector.values[inOutCount] = MAC_FUJI_REQUEST_TAG;
    }

    // Write out the magic bytes to the file so FujiNet can learn
    // the location of the I/O block

    DEBUG_STAGE("Writing");

    inOutCount = 512;
    err = FSWrite (fuji->fRefNum, &inOutCount, sector.bytes); ON_ERROR(goto cleanup);

    // Read back the file so we can learn the location of the I/O block

    DEBUG_STAGE("Seeking");

    err = SetFPos (fuji->fRefNum, fsFromStart, 0); ON_ERROR(goto cleanup);

    DEBUG_STAGE("Reading back sector");

    inOutCount = sizeof(unsigned long) * 2;
    err = FSRead (fuji->fRefNum, &inOutCount, sector.bytes); ON_ERROR(goto cleanup);

    if (sector.values[0] == MAC_FUJI_REPLY_TAG) {
        sectorAddr = sector.values[1];
        #if DEBUG
            printf("Got magic LBA: %ld", sectorAddr);
        #endif
    } else {
        #if DEBUG
            printf("Failed to get LBA: ");
            for(i = 0; i < 8; i++) {
                printf("%02x ", (unsigned char)sector.bytes[i]);
            }
            printf("\n");
        #endif
        err = -1;
        goto cleanup;
    }

    fuji->iopb.ioRefNum     = drvrRefNum;
    fuji->iopb.ioCompletion = 0;
    fuji->iopb.ioBuffer     = sector.bytes;
    fuji->iopb.ioReqCount   = 512;
    fuji->iopb.ioPosMode    = fsFromStart;
    fuji->iopb.ioPosOffset  = 512L * (long) sectorAddr;
    fuji->iopb.ioVRefNum    = driveNum;

cleanup:
    //DEBUG_STAGE("Enabling Cache\n");
    //err = sonyTrackCacheControl(driveNum, drvrRefNum, sonyEnableCache); ON_ERROR();

    return err;
}