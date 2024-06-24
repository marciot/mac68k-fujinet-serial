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

#pragma once

#include <stddef.h>

#define USE_WRITE_BUFFER 1

#define FUJI_DRVR_NAME "\p.Fuji"  // Only installed if STANDALONE_FUJI_DRIVER is 1
#define MODEM_OUT_NAME "\p.AOut"
#define MODEM_IN__NAME "\p.AIn"
#define PRNTR_OUT_NAME "\p.BOut"
#define PRNTR_IN__NAME "\p.BIn"
#define MACTCP_IP_NAME "\p.IPP"


#define MAC_FUJI_KNOCK_SEQ     {0,70,85,74,73}   // Sequence of magic sector access
#define MAC_FUJI_KNOCK_LEN     5                 // Length of magic sequence
#define MAC_FUJI_NDEV_FILE     "\pFujiNet.ndev"  // Pascal string, filename
#define MAC_FUJI_CREATOR       'FUJI'            // OSType, creator for device file
#define MAC_FUJI_TYPE          'TEXT'            // OSType, type for device file
#define MAC_FUJI_REQUEST_TAG   'NDEV'            // OSType, tag marking FujiNet request
#define MAC_FUJI_REPLY_TAG     'FUJI'            // OSType, tag marking FujiNet reply
#define MAC_FUJI_POLL_INTERVAL 60

#define MIN(a,b) ((a < b) ? a : b)
#define MAX(a,b) ((a > b) ? a : b)
#define STATIC_ASSERT(COND,MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
#define MEMBER_SIZE(TYPE,MEMBER) sizeof(((TYPE*)0)->MEMBER)

#define NELEMENTS(a) (sizeof(a)/sizeof(a[0]))

struct DriverInfo {
	short              refNum;
	IOParam           *pendingPb;
	DCtlEntry         *pendingDce;
};

struct FujiConData {
	volatile IOParam   iopb;
	short              fRefNum;
} ;

struct StorageSpec {
	// WARNING: The ordering and size of this data structure must
	//          match the corresponding fields in IOParam

	Ptr           ioBuffer;
	volatile long ioReqCount;
	volatile long ioActCount;
};

struct FujiSerData {
	struct FujiConData conn;
	OSType             id;

	/* Length of driver info table is one more than the
	 * number of drivers we may override:
	 *
	 * (.Fuji, .AOut, .AIn, .BOut., .Bin, .IPP) + 1 = 7
	 */
	struct DriverInfo  drvrInfo[7];

	struct {
		OSType         id;
		char           src;
		char           dst;
		short          avail;
		long           reserved;
		char           payload[500];
	} readData;

	struct StorageSpec readStorage;
	unsigned long      readExtraAvail;

	volatile Boolean   inWakeUp;

	long               bytesWritten;
	long               bytesRead;

	unsigned char vblCount;

	#if USE_WRITE_BUFFER
		struct {
			OSType     id;
			char       src;
			char       dst;
			short      length;
			long       reserved;
			char       payload[500];
		} writeData;

		struct StorageSpec writeStorage;
	#endif
} ;

typedef struct FujiSerData **FujiSerDataHndl;

typedef union {
	char    bytes[512 ];
	OSType values[512 / sizeof(OSType)];
} SectorBuffer;

typedef union {
	char bytes[20];
	struct {
		// Format of FujiNet sector tags
		OSType          id;
		unsigned char   vdev;
		unsigned char   cmd;
		short           len;
	} msg;
	struct {
		// Format of MacOS sector tags
		unsigned long fileNum;
		char          forkType;
		char          fileAttr;
		short         relBlkNum;
		unsigned long absBlkNum;
	} fsTags;
} TagBuffer;

FujiSerDataHndl getFujiSerialDataHndl (void);

#ifdef THINK_C
	#define Declare_LoMem(type, name, address)  type (name) : (address)
#elif defined(THINK_CPLUS)
	#define Declare_LoMem(type, name, address)  static type &(name) = *(type *) (address)
#else
	#error LoMem requires either C++ or THINK C
#endif

Declare_LoMem(volatile unsigned long,  Ticks,       0x16A);
Declare_LoMem(volatile unsigned long,  UTableBase,  0x11C);
Declare_LoMem(volatile unsigned short, UnitNtryCnt, 0x1D2);
Declare_LoMem(volatile unsigned long,  ScrnBase,    0x824);
Declare_LoMem(volatile unsigned long,  BufTgFNum,   0x2FC);
Declare_LoMem(volatile unsigned short, BufTgFFlag,  0x300);
Declare_LoMem(volatile unsigned short, BufTgFBkNum, 0x302);
Declare_LoMem(volatile unsigned long,  BufTgDate,   0x304);

#define FUJI_TAG_ID   BufTgFNum
#define FUJI_TAG_SRC  BufTgFFlag
#define FUJI_TAG_LEN  BufTgFBkNum

STATIC_ASSERT( MEMBER_SIZE(struct FujiSerData, readData)  == 512 , fuji_ser_data_r_size);
STATIC_ASSERT( MEMBER_SIZE(struct FujiSerData, writeData) == 512 ,fuji_ser_data_w_size);
STATIC_ASSERT( offsetof(struct StorageSpec,ioBuffer)   == 0, ss_test_1);
STATIC_ASSERT( offsetof(struct StorageSpec,ioReqCount) == (offsetof(IOParam,ioReqCount) - offsetof(IOParam,ioBuffer)), ss_test_2);
STATIC_ASSERT( offsetof(struct StorageSpec,ioActCount) == (offsetof(IOParam,ioActCount) - offsetof(IOParam,ioBuffer)), ss_test_3);
STATIC_ASSERT( MEMBER_SIZE(struct StorageSpec,ioReqCount) == MEMBER_SIZE(IOParam,ioReqCount), ss_test_4);
STATIC_ASSERT( MEMBER_SIZE(struct StorageSpec,ioActCount) == MEMBER_SIZE(IOParam,ioActCount), ss_test_5);