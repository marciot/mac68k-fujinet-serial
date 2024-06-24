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

// Higher-level access via the serial drivers

OSErr   fujiSerialInstall (void);
OSErr   fujiSerialRedirectModem (void);
OSErr   fujiSerialRedirectPrinter (void);
OSErr   fujiSerialRedirectMacTCP (void);
OSErr   fujiSerialOpen (short vRefNum);
Boolean fujiSerialStats (unsigned long *bytesRead, unsigned long *bytesWritten);

Boolean isFujiConnected(void);
Boolean isFujiSerialInstalled(void);
Boolean isFujiModemRedirected(void);
Boolean isFujiPrinterRedirected(void);
Boolean isFujiMacTCPRedirected(void);

// Low-level access via the floppy port

Boolean fujiReady (struct FujiConData *);
OSErr   fujiInit  (struct FujiConData *);
OSErr   fujiOpen  (struct FujiConData *, short vRefNum);