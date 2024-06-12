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

#include <Dialogs.h>
#include <Devices.h>

#include "FujiNet.h"
#include "FujiDebugMacros.h"
#include "FujiInterfaces.h"

#define STANDALONE_FUJI_DRIVER 1 // Install a separate ".Fuji" driver

#define FUJI_MAIN_RSRC "\p.FujiMain"
#define FUJI_STUB_RSRC "\p.FujiStub"
#define FUJI_STUB_HOFF 0x0022 // Offset to drvrHndl in stub driver

#if STANDALONE_FUJI_DRIVER

    // Inside Macintosh: Devices: Listing 1-14
    static short findSpaceInUnitTable () {
        Ptr   curUTableBase,    newUTableBase;
        short curUTableEntries, newUTableEntries;
        short refNum, unitNum;

        // Get current unit table values from low memory globals
        curUTableEntries = UnitNtryCnt;
        curUTableBase    = (Ptr)UTableBase;

        // Search for empty space in unit table
        for (unitNum = curUTableEntries - 1; unitNum >= 48; unitNum--) {
            refNum = ~(unitNum);
            if (GetDCtlEntry(refNum) == 0L) {
                return unitNum;
            }
        }

        // no space in the current table, so make a new one

        // increase the size of th table by 4 (an arbitrary value)
        newUTableEntries = curUTableEntries + 4;

        // allocate space for the new table
        newUTableBase = NewPtrSysClear ((long)newUTableEntries * sizeof(Handle));
        if (newUTableBase == NULL) {
            return MemError();
        }

        // copy the old table to the new table
        BlockMove (curUTableBase, newUTableBase, (long)curUTableEntries * sizeof(Handle));

        // set the new unit table values in low memory
        UTableBase  = (unsigned long)newUTableBase;
        UnitNtryCnt = newUTableEntries;

        unitNum = newUTableEntries - 1;
        return unitNum;
    }
#endif

/**
 * Finds the dce and driver header for a particular unit number
 */
static Boolean getDCE (short unitNum, DCtlEntry **_dce, DRVRHeader **_drvrHdl) {
    Handle *table = (Handle*) UTableBase;
    if (table[unitNum]) {
        DCtlEntry *dce = (DCtlEntry*) *table[unitNum];
        const Boolean dRamBased = dce->dCtlFlags & dRAMBasedMask;
        DRVRHeader *drvrHdl = (DRVRHeader*) (dRamBased ? *(Handle)dce->dCtlDriver : dce->dCtlDriver);
        *_dce     = dce;
        *_drvrHdl = drvrHdl;
        return true;
    }
    return false;
}

static short findUnitNumberByName (ConstStr255Param drvrName) {
    short i;
    for (i = 0; i < UnitNtryCnt; i++) {
        DCtlEntry *dce;
        DRVRHeader *drvrHdl;
        if (getDCE(i, &dce, &drvrHdl)) {
            if (EqualString (drvrName, drvrHdl->drvrName, false, true)) {
                return i;
            }
        }
    }
    return -1;
}

/**
 * Checks whether a particular driver has been replaced with a FujiNet driver,
 * by looking for a magic number in the first long word of the driver storage.
 * If so, returns the driver storage.
 */
static FujiSerDataHndl getSerialDataHndl (ConstStr255Param drvrName) {
    DCtlEntry  *dce;
    DRVRHeader *header;
    const short unitNumber = findUnitNumberByName(drvrName);
    return (
        (unitNumber != -1) &&
        (getDCE(unitNumber, &dce, &header)) &&
        (dce->dCtlFlags & dRAMBasedMask) &&
        (dce->dCtlStorage != NULL) &&
        ((*(FujiSerDataHndl)dce->dCtlStorage)->id == 'FUJI')
       ) ? (FujiSerDataHndl)dce->dCtlStorage : NULL;
}

FujiSerDataHndl getFujiSerialDataHndl () {
    #if STANDALONE_FUJI_DRIVER
        return getSerialDataHndl (FUJI_DRVR_NAME);
    #else
        return getSerialDataHndl (MODEM_OUT_NAME);
    #endif
}

/**
 * Allocate a new storage block for the FujiNet driver. The storage
 * will be shared by the input and output drivers.
 */
static FujiSerDataHndl newFujiSerialDataHandle () {
    FujiSerDataHndl hndl = (FujiSerDataHndl) NewHandleSysClear(sizeof(struct FujiSerData));
    if (hndl != NULL) {
        (*hndl)->id = 'FUJI';
        fujiInit (&(*hndl)->conn);
    }
    return hndl;
}

/**
 * Loads a named resource in the system heap.
 */
static Handle LoadDriverResource (ResType rType, ConstStr255Param rName) {
    Handle rHand = GetNamedResource (rType, rName);
    if (rHand) {
        #if DEBUG
            if (HandleZone (rHand) != SystemZone()) {
                printf("Driver needs 'System Heap' flag set\n");
                return 0;
            }
        #endif

        DetachResource (rHand);

        #if DEBUG
            if (ResError()) {
                printf("Detach resource failed (%d)\n", ResError());
            }
        #endif
    }
    #if DEBUG
        else {
            printf("Failed to load resoruce (%d)\n", ResError());
        }
    #endif
    return rHand;
}

/* Either replace or create a new DCE in the device unit table, pointing
 * it to a RAM-based driver.
 */
static OSErr installDCE (short unitNum, Handle drvrHdl, Handle drvrStorage) {
    DCtlEntry  *dce;
    Handle *table = (Handle*) UTableBase;

    if (table[unitNum] == NULL) {
        // Create the DCE
        Handle dceHdl;
        ReserveMemSys (sizeof(DCtlEntry));
        dceHdl = NewHandleSysClear (sizeof(DCtlEntry));
        HLock (dceHdl);
        if (dceHdl == NULL) {
            return MemError();
        }

        dce = (DCtlEntry*) *dceHdl;
        dce->dCtlFlags   = ((DRVRHeader*)*drvrHdl)->drvrFlags;
        dce->dCtlRefNum  = ~unitNum;

        table[unitNum] = dceHdl;
    } else {
        dce = (DCtlEntry*) *table[unitNum];
    }

    dce->dCtlFlags   = dRAMBasedMask | ((DRVRHeader*)*drvrHdl)->drvrFlags;
    dce->dCtlDelay   = ((DRVRHeader*)*drvrHdl)->drvrDelay;
    dce->dCtlDriver  = (Ptr) drvrHdl;
    dce->dCtlStorage = drvrStorage;
    return noErr;
}

/* Replace a serial driver with a stub driver that
 * forwards requests to the main FujiNet driver.
 */
static OSErr installStubDriver (ConstStr255Param stubName) {
    OSErr          err = noErr;
    DCtlEntry      *fujiDCE;
    DRVRHeader     *fujiHdr;
    unsigned long *stubHndlStorage;
    Handle         stubHndl;

    #if STANDALONE_FUJI_DRIVER
        const short fujiNum = findUnitNumberByName(FUJI_DRVR_NAME);
    #else
        const short fujiNum = findUnitNumberByName(MODEM_OUT_NAME);
    #endif
    const short stubNum = findUnitNumberByName(stubName);

    if ( (fujiNum != -1) && (stubNum != -1) ) {
        // Load the stub driver

        Handle stubHndl = LoadDriverResource ('DRVR', FUJI_STUB_RSRC);
        if (stubHndl == NULL) {
            err = ResError();
            goto error;
        }

        // Store a copy of the Fuji driver's handle in the stub driver

        getDCE (fujiNum, &fujiDCE, &fujiHdr);

        stubHndlStorage = (unsigned long*) ( ((char*)*stubHndl) + FUJI_STUB_HOFF );
        #if DEBUG
            if (*stubHndlStorage != 0x01234567) {
                printf ("Unable to find magic number in stub driver\n");
                err = -1;
                goto error;
            }
        #endif
        *stubHndlStorage = (unsigned long) fujiDCE->dCtlDriver;

        // Adjust the stub driver name and copy flags from main Fuji driver.

        BlockMove (stubName, ((DRVRHeader*)*stubHndl)->drvrName, stubName[0] + 1);
        ((DRVRHeader*)*stubHndl)->drvrDelay = fujiHdr->drvrDelay;
        ((DRVRHeader*)*stubHndl)->drvrFlags = fujiHdr->drvrFlags;

        err = installDCE (stubNum, stubHndl, fujiDCE->dCtlStorage);
        if (err) {
            goto error;
        }
    } else {
        return -1;
    }
    return err;

error:
    if (stubHndl) {
        DisposHandle ((Handle)stubHndl);
    }
    return err;
}

static OSErr installStubDrivers (ConstStr255Param outDrvrName, ConstStr255Param inDrvrName) {
    // Initialize in and out drivers

    OSErr err = installStubDriver (outDrvrName);
    if (err != noErr) {
        return err;
    }
    return installStubDriver (inDrvrName);
}

OSErr fujiSerialInstall () {
    OSErr           err = noErr;
    Handle          fujiHndl = 0;
    FujiSerDataHndl fujiData = 0;
    short           fujiNum  = 0;

    if (!isFujiSerialInstalled()) {

        // Load the driver and allocate driver data

        fujiHndl = LoadDriverResource ('DRVR', FUJI_MAIN_RSRC);
        if (fujiHndl == NULL) {
            err = ResError();
            goto error;
        }

        fujiData = newFujiSerialDataHandle();
        if (fujiData == NULL) {
            err = MemError();
            goto error;
        }

        #if STANDALONE_FUJI_DRIVER
            // Find space in the unit table

            fujiNum = findSpaceInUnitTable();
            if (fujiNum < 0) {
                err = openErr;
                goto error;
            }

            err = installDCE (fujiNum, fujiHndl, (Handle)fujiData);
            if (err) {
                goto error;
            }

            (*fujiData)->mainDrvrRefNum = fujiNum;
        #else
            // Install the main Fuji driver as the serial out driver

            BlockMove (MODEM_OUT_NAME, ((DRVRHeader*)*fujiHndl)->drvrName, MODEM_OUT_NAME[0] + 1);

            fujiNum = findUnitNumberByName(MODEM_OUT_NAME);
            if (fujiNum == -1) {
                err = -1;
                goto error;
            }
            err = installDCE (fujiNum, fujiHndl, (Handle)fujiData);
            if (err) {
                goto error;
            }

            (*fujiData)->mainDrvrRefNum = fujiNum;

            // Install a stub driver as the serial in driver

            err = installStubDriver (MODEM_IN__NAME);
        #endif
    }
    return err;

error:
    if (fujiHndl) {
        DisposHandle ((Handle)fujiHndl);
    }
    if (fujiData) {
        DisposHandle ((Handle)fujiData);
    }
    return err;
}

Boolean isFujiSerialInstalled() {
    return getFujiSerialDataHndl () != NULL;
}

Boolean isFujiModemRedirected() {
    return getSerialDataHndl (MODEM_OUT_NAME) != NULL;
}

Boolean isFujiPrinterRedirected() {
    return getSerialDataHndl (PRNTR_OUT_NAME) != NULL;
}

Boolean isFujiConnected() {
    FujiSerDataHndl data = getFujiSerialDataHndl ();
    return data ? fujiReady (&(*data)->conn) : false;
}

OSErr fujiSerialRedirectModem () {
    return installStubDrivers (MODEM_OUT_NAME, MODEM_IN__NAME);
}

OSErr fujiSerialRedirectPrinter () {
    return installStubDrivers (PRNTR_OUT_NAME, PRNTR_IN__NAME);
}

Boolean fujiSerialStats (unsigned long *bytesRead, unsigned long *bytesWritten) {
    FujiSerDataHndl data = getFujiSerialDataHndl ();
    if (data) {
        *bytesRead    = (*data)->bytesRead;
        *bytesWritten = (*data)->bytesWritten;
        return true;
    } else {
        *bytesRead    = 0;
        *bytesWritten = 0;
        return false;
    }
}

OSErr fujiSerialOpen (short vRefNum) {
    OSErr err;
    FujiSerDataHndl data;
    if (!isFujiSerialInstalled()) {
        err = fujiSerialInstall ();
        if (err != noErr) {
            return err;
        }
    }
    data = getFujiSerialDataHndl ();
    if (data) {
        HLock((Handle)data);
        err = fujiOpen (&(*data)->conn, vRefNum);
        HUnlock((Handle)data);
        return err;
    }
}
