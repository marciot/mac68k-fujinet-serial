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

#include <stddef.h>

#include <Devices.h>
#include <Files.h>
#include <Serial.h>
#include <Retrace.h>
#include <MacTCP.h>

#include "FujiNet.h"
#include "FujiInterfaces.h"

// Configuration options

#define SANITY_CHECK      1 // Do additional error checking
#define USE_AOUT_EXTRAS   0
#define USE_IPP_UDP       0
#define USE_IPP_TCP       0

#define VBL_TICKS         30 // Note, setting this to 15 can cause issues

// Menubar "led" indicators

#define LED_IDLE       ind_hollow
#define LED_ASYNC_IO   ind_solid
#define LED_BLKED_IO   ind_dot
#define LED_WRONG_TAG  ind_ring
#define LED_ERROR      ind_cross

#define VBL_WRIT_INDICATOR(symb) drawIndicatorAt (496, 1, symb);
#define VBL_READ_INDICATOR(symb) drawIndicatorAt (496, 9, symb);

// Driver flags and prototypes

#define DFlags dWritEnableMask | dReadEnableMask | dStatEnableMask | dCtlEnableMask | dNeedLockMask
#define JIODone 0x08FC

OSErr doOpen    (   IOParam *, DCtlEntry *);
OSErr doPrime   (   IOParam *, DCtlEntry *);
OSErr doClose   (   IOParam *, DCtlEntry *);
OSErr doControl (CntrlParam *, DCtlEntry *);
OSErr doStatus  (CntrlParam *, DCtlEntry *);

/**
 * The "main" function must be the 1st defined in the file
 *
 * To reduce the code size, we use our own entry rather than
 * the Symantec C++ provided stub.
 *
 * To compile:
 *
 *  - From "Project" menu, select "Set Project Type..."
 *  - Set to "Code Resource"
 *  - Set file type to "rsrc" and creator code to "RSED"
 *  - Set the name to ".FujiMain"
 *  - Set the Type to 'DRVR'
 *  - Set ID to -15904
 *  - Check "Custom Header"
 *  - In "Attrs", set to "System Heap" (40)
 *
 */

void main() {
	asm {

		// Driver Header: "Inside Macintosh: Devices", p I-25

		dc.w    DFlags                             ; flags
		dc.w    60                                 ; periodic ticks
		dc.w    0x0000                             ; DA event mask
		dc.w    0x0000                             ; menuID of DA menu
		dc.w    @DOpen    +  8                     ; open offset
		dc.w    @DPrime   + 10                     ; prime offset
		dc.w    @DControl + 12                     ; control offset
		dc.w    @DStatus  + 14                     ; status offset
		dc.w    @DClose   + 16                     ; close offset
		dc.b    "\p.Fuji"                          ; driver name

		// Driver Dispatch: "Inside Macintosh: Devices", p I-29

	DOpen:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doOpen                             ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		rts

	DPrime:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doPrime                            ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		bra     @IOReturn

	DControl:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doControl                          ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		cmpi.w  #killCode,CntrlParam.csCode(a0)    ; test for KillIO call (special case)
		bne     @IOReturn
		rts                                        ; KillIO must always return via RTS

	DStatus:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doStatus                           ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr

	IOReturn:
		move.w  CntrlParam.ioTrap(a0),d1
		btst    #noQueueBit,d1                     ; immediate calls are not queued, and must RTS
		beq     @Queued                            ; branch if queued

	NotQueued:
		tst.w   d0                                 ; test asynchronous return result
		ble     @ImmedRTS                          ; result must be <= 0
		clr.w   d0                                 ; "in progress" result (> 0) not passed back

	ImmedRTS:
		move.w  d0,IOParam.ioResult(a0)            ; for immediate calls you must explicitly
												   ; place the result in the ioResult field
		rts

	Queued:
		tst.w   d0                                 ; test asynchronous return result
		ble     @MyIODone                          ; I/O is complete if result <= 0
		clr.w   d0                                 ; "in progress" result (> 0) not passed back
		rts

	MyIODone:
		move.l  JIODone,-(SP)                      ; push IODone jump vector onto stack
		rts

	DClose:
		movem.l a0-a1,-(sp)                        ; save ParmBlkPtr, DCtlPtr across function call
		movem.l a0-a1,-(sp)                        ; push ParmBlkPtr, DCtlPtr for C
		bsr     doClose                            ; call linked C function
		addq    #8,sp                              ; clean up the stack
		movem.l (sp)+,a0-a1                        ; restore ParmBlkPtr, DCtlPtr
		;rts                                       ; close is always immediate, must return via RTS
	}
}

#include "LedIndicators.h" // Don't put this above main as it genererates code

/********** Completion and VBL Routines **********/

static void fujiStartVBL (DCtlEntry *devCtlEnt);

static VBLTask   *getVBLTask (void);
static DCtlEntry *getMainDCE (void);

static void schedVBLTask (void); // Schedule the VBL task to run ASAP

// Completion routines

static void complFlushOut (void);  // calls emptyWriteBufDone
static void complReadIn (void);    // calls fillReadBufDone

static void emptyWriteBufDone (IOParam *pb);
static void fillReadBufDone  (IOParam *pb);
static void fujiVBLTask   (VBLTask *vbl);

// When I/O is done, dispatch to JIODone

static void ioIsComplete (DCtlEntry *devCtlEnt, OSErr result);

// Mutexes

static Boolean    takeVblMutex (void);
static void       releaseVblMutex (void);

static void _vblRoutines (void) {
	asm {
		// Use extern entry point to keep Symantec C++ from adding a stack frame.

		extern fujiStartVBL:
			lea      @dcePtr, a0
			tst.l    (a0)                              ; already installed?
			bne      @skipInstall
			lea      @dcePtr, a0
			move.l   4(sp), (a0)+                      ; set dcePtr to devCtlPtr
			lea      @callFujiVBL,a1                   ; address to entry
			move.l   a1, VBLTask.vblAddr(a0)           ; update task address
			move.w   #VBL_TICKS,VBLTask.vblCount(a0)   ; reset vblCount
			_VInstall
		skipInstall:
			rts

		extern getVBLTask:
			lea      @vblTask, a0
			move.l   a0, d0
			rts

		extern schedVBLTask:
			lea      @vblTask, a0
			move.w   #1,VBLTask.vblCount(a0)           ; reset vblCount
			rts

		extern getMainDCE:
			move.l (@dcePtr), d0                       ; load DCE ptr
			rts

		dcePtr:
			dc.l     0  // Placeholder for dcePtr (4 bytes)

		vblTask:
			// VBLTask Structure (14 bytes)
			dc.l     0                                 ; qLink
			dc.w vType                                 ; qType
			dc.l     0                                 ; vblAddr
			dc.w     0                                 ; vblCount
			dc.w     0                                 ; vblPhase

		mutexFlags:
			dc.w     0

			// VBL Requirements: One entry, a0 will point to the VBLTask;
			// this routine must preserve registers other than a0-a3/d0-d3

			// ioCompletion Requirements: One entry, a0 will point to
			// parameter block and d0 contain the result; this routine
			// must preserve registers other than a0-a1/d0-d2

		extern complFlushOut:
			lea     emptyWriteBufDone,a1                   ; address of C function
			bra.s   @callRoutineC

		extern complReadIn:
			lea     fillReadBufDone,a1                    ; address of C function
			bra.s   @callRoutineC

		callFujiVBL:
			lea     fujiVBLTask,a1                     ; address of C function
			;bra.s   @callRoutineC

			// callRoutineC saves registers and passes control to the C
			// function whose address is a1 with a0 as the 1st argument
		callRoutineC:
			movem.l a2-a7/d3-d7,-(sp)                  ; save registers
			move.l a0,-(sp)                            ; push a0 for C
			jsr     (a1)                               ; call C function
			addq    #4,sp                              ; clean up the stack
			movem.l (sp)+,a2-a7/d3-d7                  ; restore registers
			rts

		extern ioIsComplete:
			move.w 8(sp),d0                            ; load result code into d0
			move.l 4(sp),a1                            ; load DCtlPtr into a1
			move.l  JIODone,-(sp)                      ; push IODone jump vector onto stack
			rts

		extern takeVblMutex:
			moveq #0, d0
			;bra.s @takeMutex

		takeMutex:
			lea @mutexFlags, a0
			bset d0, (a0)
			seq d0
			rts

		extern releaseVblMutex:
			moveq #0, d0
			;bra.s @releaseMutex

		releaseMutex:
			lea @mutexFlags, a0
			bclr d0, (a0)
			;rts
	}
}

static struct DriverInfo *getDriverInfo (struct FujiSerData *data, short dCtlRefNum) {
	struct DriverInfo *info;
	for (info = data->drvrInfo; info->refNum; ++info) {
		if (info->refNum == dCtlRefNum) {
			break;
		}
	}
	// Not found, add to the list
	info->refNum = dCtlRefNum;
	return info;
}

/* Wakes up all "FujiNet" drivers to give them a chance to complete queued I/O */

static void wakeDriversAndReleaseMutex (struct FujiSerData *data) {
	struct DriverInfo *info;

	data->inWakeUp = true;
	for (info = data->drvrInfo; info->refNum; ++info) {
		IOParam    *pb = info->pendingPb;
		DCtlEntry *dce = info->pendingDce;

		// Clear pendingPb before doPrime, as it may set it to a new value
		info->pendingPb = 0;

		if (pb) {
			const OSErr err = doPrime (pb, dce);
			if (err != ioInProgress) {
				ioIsComplete (dce, err);
			}
		}
	}
	data->inWakeUp = false;
	releaseVblMutex ();
}

static void fillReadBuffer (struct FujiSerData *data) {
	data->conn.iopb.ioMisc       = (Ptr) data;
	data->conn.iopb.ioBuffer     = (Ptr) &data->readData;
	data->conn.iopb.ioCompletion = (IOCompletionUPP) complReadIn;
	VBL_READ_INDICATOR (LED_ASYNC_IO);
	PBReadAsync ((ParmBlkPtr)&data->conn.iopb);
}

static void fillReadBufDone (IOParam *pb) {
	struct FujiSerData *data = (struct FujiSerData *)pb->ioMisc;
	long indicator = LED_ERROR;

	if (pb->ioResult == noErr) {

		if (data->readData.id == MAC_FUJI_REPLY_TAG) {
			const long readExtraAvail    = data->readData.avail - NELEMENTS(data->readData.payload);
			data->readStorage.ioActCount = 0;
			data->readStorage.ioReqCount = data->readData.avail;
			data->readExtraAvail         = 0;

			// The Pico will always report the total available bytes, even
			// when the maximum message size is 500. Store the number of bytes
			// in the read buffer in readLeft, with the overflow in readAvail.

			if (readExtraAvail > 0) {
				data->readExtraAvail         = readExtraAvail;
				data->readStorage.ioReqCount = NELEMENTS(data->readData.payload);
			}
			indicator = LED_IDLE;
		}
		else {
			indicator = LED_WRONG_TAG;
			pb->ioResult = -1;
		}
	}
	VBL_READ_INDICATOR (indicator);
	wakeDriversAndReleaseMutex (data);
}

static void emptyWriteBuffer(struct FujiSerData *data) {
	/* Figure out the source value:
	 * -6 or -7  => 1
	 * -8 or -9  => 2
	 * otherwise => 3
	 */
	//short src = ((~devCtlEnt->dCtlRefNum) - 5) >> 1;
	//if (src > 1) src = 3;

	data->conn.iopb.ioMisc       = (Ptr) data;
	data->conn.iopb.ioBuffer     = (Ptr) &data->writeData;
	data->conn.iopb.ioCompletion = (IOCompletionUPP)complFlushOut;

	data->writeData.id           = MAC_FUJI_REQUEST_TAG;
	data->writeData.src          = 0;
	data->writeData.dst          = 0;
	data->writeData.reserved     = 0;
	data->writeData.length       = data->writeStorage.ioActCount;

	VBL_WRIT_INDICATOR (LED_ASYNC_IO);
	PBWriteAsync ((ParmBlkPtr)&data->conn.iopb);
}

/* Called after an asynchronous write to the FujiNet device has completed */

static void emptyWriteBufDone (IOParam *pb) {
	struct FujiSerData *data = (struct FujiSerData *)pb->ioMisc;
	long wrIndicator = LED_ERROR;

	if (pb->ioResult == noErr) {
		data->writeStorage.ioActCount = 0;
		wrIndicator                   = LED_IDLE;

		if (data->readStorage.ioActCount == data->readStorage.ioReqCount) {
			VBL_WRIT_INDICATOR (wrIndicator);

			// After writing data, immediately do a read if the buffer is empty
			fillReadBuffer (data);
			return;
		}
	} // pb->ioResult == noErr

	VBL_WRIT_INDICATOR (wrIndicator);
	wakeDriversAndReleaseMutex (data);
}

/* Main VBL Task for the FujiNet serial driver. This task must run periodically
 * to:
 *
 *   1) check for outgoing data that needs to be written to the FujiNet device
 *   2) poll for incoming data once the read buffer is exhausted
 *   3) wake up FujiNet drivers to process queued I/O
 */

static void fujiVBLTask (VBLTask *vbl) {
	const DCtlEntry *devCtlEnt = getMainDCE();
	struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

	vbl->vblCount    = data->vblCount;

	if (takeVblMutex()) {
		if (data->conn.iopb.ioResult == noErr) {
			if (data->writeStorage.ioActCount > 0) {
				emptyWriteBuffer(data);
				return;
			}
			else if (data->readStorage.ioActCount == data->readStorage.ioReqCount) {
				fillReadBuffer(data);
				return;
			}
		} // data->conn.iopb.ioResult == noErr

		wakeDriversAndReleaseMutex (data);
	} // takeVblMutex
}

/********** Device driver routines **********/

static OSErr doControl (CntrlParam *pb, DCtlEntry *devCtlEnt) {
	//struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

	#if USE_AOUT_EXTRAS
		if (pb->csCode == 8) {
			// .AOut SerReset: Reset serial port drivers and configure the port
		}
		else if (pb->csCode == 9) {
			// .AOut SerSetBuf: Increase or restore size of serial input buffer
		}
		else if (pb->csCode == 10) {
			// .AOut SerHShake: Set handshaking options
		}
		else if (pb->csCode == 11) {
			// .AOut SetClrBrk: Deassert the break state
			//data->flags &= SER_BREAK_FLG;
		}
		else if (pb->csCode == 12) {
			// .AOut SetSetBrk: Assert the break state
			//data->flags |= SER_BREAK_FLG;
		}
		else if (pb->csCode == 13) {
			// .AOut Set Baud Rate
			//data->flags |= SER_BREAK_FLG;
		}
		else if (pb->csCode == 14) {
			// .AOut SerHShake: Set handshaking options w/ DTR
		}
		else if (pb->csCode == 16) {
			// .AOut Set Miscellaneous Options
		}
		else if (pb->csCode == 17) {
			// .AOut Assert DTR signal
		}
		else if (pb->csCode == 18) {
			// .AOut Negates DTR signal
		}
		else if (pb->csCode == 19) {
			// .AOut Simple parity error replacement
		}
		else if (pb->csCode == 20) {
			// .AOut Extended parity error replacement
		}
		else if (pb->csCode == 21) {
			// .AOut Set XOFF State
		}
		else if (pb->csCode == 22) {
			// .AOut Clear XOFF State
		}
		else if (pb->csCode == 23) {
			// .AOut Send XON Conditional
		}
		else if (pb->csCode == 24) {
			// .AOut Send XON Unconditional
		}
		else if (pb->csCode == 25) {
			// .AOut Send XOFF Conditional
		}
		else if (pb->csCode == 26) {
			// .AOut Send XOFF Unconditional
		}
		else if (pb->csCode == 27) {
			// .AOut Serial Hardware Reset
		}
	#endif
	#if USE_IPP_UDP
		if (pb->csCode == UDPCreate) { // 20
			// .IPP UDPCreate: Opens a UDP stream
		}
		else if (pb->csCode == UDPRead) { // 21
			// .IPP UDPRead: Retreives a datagram
		}
		else if (pb->csCode == UDPBfrReturn) { // 22
			// .IPP UDPBfrReturn: Returns receive buffer
		}
		else if (pb->csCode == UDPWrite) { // 23
			// .IPP UDPWrite: Sends a datagram
		}
		else if (pb->csCode == UDPRelease) { // 24
			// .IPP UDPRelease: Closes a UDP stream
		}
		else if (pb->csCode == UDPMaxMTUSize) { // 25
			// .IPP UDPMaxMTUSize: Returns the maximum size of an unfragmented datagram
		}
		else if (pb->csCode == UDPMaxMTUSize) { // 26
			// .IPP UDPStatus: Undocumented
		}
		else if (pb->csCode == UDPMultiCreate) { // 27
			// .IPP UDPMultiCreate: Creates UDP connections on a consecutive series of ports
		}
		else if (pb->csCode == UDPMultiSend) { // 28
			// .IPP UDPMultiSend: Sends a datagram from a specified port
		}
		else if (pb->csCode == UDPMultiRead) { // 29
			// .IPP UDPMultiRead: Receives data from a port created with the UDPMultiCreate
		}
	#endif
	#if USE_IPP_TCP
		if (pb->csCode == TCPCreate) { // 30
			// .IPP TCPCreate: Opens a TCP stream
		}
		else if (pb->csCode == TCPPassiveOpen) { // 31
			// .IPP TCPPassiveOpen: Listens for incoming connections
		}
		else if (pb->csCode == TCPActiveOpen) { // 32
			// .IPP TCPActiveOpen: Initiates an outgoing connection
		}
		else if (pb->csCode == TCPSend) { // 34
			// .IPP TCPSend: Sends data over the connection
		}
		else if (pb->csCode == TCPNoCopyRcv) { // 35
			// .IPP TCPNoCopyRcv: Receives data without copying
		}
		else if (pb->csCode == TCPRcvBfrReturn) { // 36
			// .IPP TCPRcvBfrReturn: Returns buffers from TCPNoCopyRcv
		}
		else if (pb->csCode == TCPRcv) { // 37
			// .IPP TCPRcv: Receives data and copy to user buffers
		}
		else if (pb->csCode == TCPClose) { // 38
			// .IPP TCPClose: Signals user has no more data to send on connection
		}
		else if (pb->csCode == TCPAbort) { // 39
			// .IPP TCPAbort: Terminates a connection without attempting to send all outstanding data
		}
		else if (pb->csCode == TCPStatus) { // 40
			// .IPP TCPStatus: Gather information about a specific connection
		}
		else if (pb->csCode == TCPExtendedStat) { // 41
			// .IPP TCPExtendedStat: Undocumented
		}
		else if (pb->csCode == TCPRelease) { // 42
			// .IPP TCPRelease: Closes a TCP stream
		}
		else if (pb->csCode == TCPGlobalInfo) { // 43
			// .IPP TCPGlobalInfo: Retreive TCP parameters
		}
	#endif

	//HUnlock (data->fuji);
	//HUnlock (devCtlEnt->dCtlStorage);
	//HUnlock ((Handle)devCtlEnt->dCtlDriver);
	return noErr;
}

static OSErr doStatus (CntrlParam *pb, DCtlEntry *devCtlEnt) {
	struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;

	if (pb->csCode == 2) {

		// SetGetBuff: Return how much data is available

		pb->csParam[0] = 0;                                // High order-word
		pb->csParam[1] = (data->readStorage.ioReqCount - data->readStorage.ioActCount) + data->readExtraAvail; // Low order-word

	}
	#if USE_AOUT_EXTRAS
		else if (pb->csCode == 8) {

			// SerStatus: Obtain status information from the serial driver

			SerStaRec *status = (SerStaRec *) &pb->csParam[0];
			status->rdPend  = 0;
			status->wrPend  = 0;
			status->ctsHold = 0;
			status->cumErrs = 0;
			status->cumErrs  = 0;
			status->xOffSent = 0;
			status->xOffHold = 0;

		} else if (pb->csCode == 9) {
			// .AOut Serial Driver Version
		}
	#endif
	//HUnlock (data->fuji);
	//HUnlock (devCtlEnt->dCtlStorage);
	//HUnlock ((Handle)devCtlEnt->dCtlDriver);
	return noErr;
}

static void bufferCopy (struct StorageSpec *src, struct StorageSpec *dst) {
	const long srcLeft = src->ioReqCount - src->ioActCount;
	long       dstLeft = dst->ioReqCount - dst->ioActCount;

	#if SANITY_CHECK
		if (dst->ioReqCount < 0) {
			SysBeep(10);
			dstLeft = 0;
		}

		if (dst->ioActCount < 0) {
			SysBeep(10);
			dstLeft = 0;
		}

		if (bytesToProcess < 0) {
			SysBeep(10);
			dstLeft = 0;
		}
	#endif

	if (dstLeft > srcLeft) {
		dstLeft = srcLeft;
	}
	if (dstLeft > 0) {
		BlockMove (
			src->ioBuffer + src->ioActCount,
			dst->ioBuffer + dst->ioActCount,
			dstLeft
		);
	}
	src->ioActCount += dstLeft;
	dst->ioActCount += dstLeft;
}

static OSErr doPrime (IOParam *pb, DCtlEntry *devCtlEnt) {
	struct FujiSerData *data = *(FujiSerDataHndl)devCtlEnt->dCtlStorage;
	OSErr err = ioInProgress;

	if (data->inWakeUp || takeVblMutex()) {
		if (data->conn.iopb.ioResult != noErr) {
			err = data->conn.iopb.ioResult;
		} else {
			const unsigned char cmd = pb->ioTrap & 0x00FF;
			struct StorageSpec *src = 0, *dst;
			if (cmd == aRdCmd) {
				src = &data->readStorage;
				dst = (struct StorageSpec*) &pb->ioBuffer;
			} else if (cmd == aWrCmd) {
				src = (struct StorageSpec*) &pb->ioBuffer;
				dst = &data->writeStorage;
			}
			if (src) {
				bufferCopy (src, dst);
			}
			if (pb->ioActCount == pb->ioReqCount) {
				err = noErr;

				if (cmd == aWrCmd) {
					data->bytesWritten += pb->ioActCount;
				} else {
					data->bytesRead    += pb->ioActCount;
				}
			}
		}

		if (!data->inWakeUp) {
			releaseVblMutex();
		}
	} // takeVblMutex

	if (err == ioInProgress) {
		// Make a record that we are suspended so we can get awoken
		struct DriverInfo *info = getDriverInfo (data, pb->ioRefNum);
		info->pendingDce = devCtlEnt;
		info->pendingPb  = pb;
		schedVBLTask();
	}

	pb->ioResult = err;
	return err;
}

static OSErr doOpen (IOParam *pb, DCtlEntry *dce) {
	struct FujiSerData *data;

	// Make sure the dCtlStorage was populated by the FujiNet DA

	if (dce->dCtlStorage == 0L) {
		return openErr;
	}

	HLock (dce->dCtlStorage);

	// Make sure the port is configured correctly

	data = *(FujiSerDataHndl)dce->dCtlStorage;
	if (data->conn.iopb.ioRefNum == 0L) {
		return portNotCf;
	}

	// Figure out which driver we are opening
	//if (data->mainDrvrRefNum == dce->dCtlRefNum) {
	//  dce->dCtlFlags |= dNeedLockMask;
	//}

	// Start the VBL task
	data->conn.iopb.ioResult = noErr;

	if (data->vblCount == 0) {
		data->vblCount = VBL_TICKS;
	}

	data->readStorage.ioBuffer    = data->readData.payload;
	data->readStorage.ioReqCount  = 0;
	data->readStorage.ioActCount  = 0;

	data->writeStorage.ioBuffer   = data->writeData.payload;
	data->writeStorage.ioReqCount = NELEMENTS(data->writeData.payload);
	data->writeStorage.ioActCount = 0;

	fujiStartVBL (dce);

	return noErr;
}

static OSErr doClose (IOParam *pb, DCtlEntry *devCtlEnt) {
	return noErr;
}