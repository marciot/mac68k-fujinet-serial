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

enum {
	ind_hollow,
	ind_gray,
	ind_solid,
	ind_dot,
	ind_ring,
	ind_cross
};

static pascal void drawIndicatorAt(int x, int y, long which);

/**
 * Fills in a 7 pixel diameeter circular "LED" indicator
 * with one of six patterns to indicate a status or error
 * condition. The position x must be a multiple of 8.
 *
 * Each symbol is defined by four bytes (representing 8
 * monocrome pixels) which are copied to the screen top
 * to bottom in the sequence 0,1,2,3,3,2,1,0.
 */

static pascal void drawIndicatorAt (int x, int y, long which);

static pascal void _drawIndicatorAt() {
	asm {
		empty:
			dc.b 0x38
			dc.b 0x44
			dc.b 0x82
			dc.b 0x82
		gray:
			dc.b 0x38
			dc.b 0x54
			dc.b 0xAA
			dc.b 0xD6
		solid:
			dc.b 0x38
			dc.b 0x7C
			dc.b 0xFE
			dc.b 0xFE
		dot:
			dc.b 0x38
			dc.b 0x44
			dc.b 0xBA
			dc.b 0xBA
		ring:
			dc.b 0x38
			dc.b 0x7C
			dc.b 0xEE
			dc.b 0xC6
		cross:
			dc.b 0x38
			dc.b 0x54
			dc.b 0x92
			dc.b 0xFE

		extern drawIndicatorAt:
			movem.l (sp)+,d0-d2     ; d0 = return; d1 = which; d2 = x/y
			move.l d0,-(sp)         ; push back return address
			movea.l (ScrnBase),a0   ; load contents of screen base
			lsr.w #3,d2             ; x /= 8
			adda.w d2,a0            ; add to base offset
			swap d2                 ; load y
			lsl.w #6,d2             ; y *= 512/8
			adda.w d2,a0            ; add to base offset
		rDrawIndicator:
			// d1: Index of icon (1-6)
			// a0: ptr to screen location
			lsl.l #2,d1             ; multiply offsets by 4
			lea @empty(d1.w),a1     ; find index of icon
			lea 512(a0),a2          ; start a2 from bottom of icon
			moveq.l #64,d2          ; stride for 512 pixels
		writeByte:
			adda.l d2,a0
			suba.l d2,a2
			move.b (a1), (a0)
			move.b (a1)+,(a2)
			cmpa.l a0,a2
			bne @writeByte
			;rts
	}
}