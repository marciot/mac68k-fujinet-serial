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

/**
 * The "main" function must be the 1st defined in the file
 *
 * This device driver is used for ".AOut", ".AIn", ".BOut" and ".BIn".
 * Because it is loaded into memory multiple times, it is meant to be
 * a small stub that passes control to the main ".Fuji" serial driver,
 * whose's handle must be stored at the longword at "drvrHndl"
 *
 * To compile:
 *
 *  - From "Project" menu, select "Set Project Type..."
 *  - Set to "Code Resource"
 *  - Set file type to "rsrc" and creator code to "RSED"
 *  - Set the name to ".FujiStub"
 *  - Set the Type to 'DRVR'
 *  - Set ID to -15903
 *  - Check "Custom Header"
 *  - In "Attrs", set to "System Heap" (40)
 *
 */

#include <Devices.h>

#define DFlags dWritEnableMask | dReadEnableMask | dStatEnableMask | dCtlEnableMask

void main() {
    asm {

        // Driver Header: "Inside Macintosh: Devices", p I-25

        dc.w    DFlags                          ; flags
        dc.w    0x0000                          ; periodic ticks
        dc.w    0x0000                          ; DA event mask
        dc.w    0x0000                          ; menuID of DA menu
        dc.w    @DOpen    +  8                  ; open offset
        dc.w    @DPrime   + 10                  ; prime offset
        dc.w    @DControl + 12                  ; control offset
        dc.w    @DStatus  + 14                  ; status offset
        dc.w    @DClose   + 16                  ; close offset
        dc.b    "\p.Fuji"                       ; driver name

    DOpen:    bsr.s @Dispatch
    DPrime:   bsr.s @Dispatch
    DControl: bsr.s @Dispatch
    DStatus:  bsr.s @Dispatch
    DClose:   bsr.s @Dispatch

    drvrHndl: dc.l 0x01234567                   ; placeholder for driver handle

    Dispatch:
        move.l      (sp)+,d0                    ; pop return address into d0
        move.l         a2,-(sp)                 ; save registers
        lea       @DPrime,a2                    ; subtract address of DPrime from return address
        sub.l          a2,d0                    ; ...d0 will be 0,2,4,6,8 for Open, Prime, Control...
        movea.l  @drvrHndl,a2                   ; get destination driver handle
        movea.l       (a2),a2                   ; dereference to driver header pointer
        move.w DRVRHeader.drvrOpen(a2,d0),d0    ; get entry routine offset from driver header
        add.l          a2,d0                    ; add driver base pointer to offset
        move.l      (sp)+,a2                    ; restore registers
        move.l         d0,-(sp)                 ; push address to driver routine
       ;rts                                     ; jump to driver routine
    }
}