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

#include <MacTCP.h>

#include "FujiTests.h"
#include "FujiDebugMacros.h"

#include "compat.h"
#include "TCPHi.h"
#include "TCPRoutines.h"

Boolean gCancel = false;	/* this is set to true if the user cancels an operation */

Boolean GiveTime(short sleepTime) {
	/*SpinCursor(1);*/
	return true;
}

OSErr testBasicTCP (void) {
	unsigned long stream;
	OSErr err;

	err = InitNetwork (); CHECK_ERR;

	err = CreateStream (&stream, 1024); CHECK_ERR;
	err = ReleaseStream (stream); CHECK_ERR;
}