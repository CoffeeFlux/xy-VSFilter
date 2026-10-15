/*
 *	Copyright (C) 2003-2006 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include <atlbase.h>
#include <atlcoll.h>

enum ColorType {    
    MSP_RGB32,
    MSP_RGB24,
    MSP_RGB16,
    MSP_RGB15,
    MSP_YUY2,
    MSP_YV12,
    MSP_IYUV,
    MSP_AYUV,
    MSP_RGBA,        //pre-multiplied alpha. Use A*g + RGB to mix
    MSP_RGBA_F,      //pre-multiplied alpha. Use (0xff-A)*g + RGB to mix
    MSP_AYUV_PLANAR, //AYUV in planar form
    MSP_XY_AUYV,
    MSP_P010,
    MSP_P016,
    MSP_NV12,
    MSP_NV21,
    MSP_P210, // 4:2:2 10 bits
    MSP_P216, // 4:2:2 16 bits
    MSP_YV16, // 4:2:2 8 bits
    MSP_YV24  // 4:4:4 8 bits

};

#pragma pack(push, 1)
struct SubPicDesc {
	int type;
	int w, h, bpp, pitch, pitchUV;
	BYTE* bits;
	BYTE* bitsU;
	BYTE* bitsV;
	RECT vidrect; // video rectangle

	SubPicDesc() {
		type = 0;
		w = h = bpp = pitch = pitchUV = 0;
		bits = NULL;
		bitsU = bitsV = NULL;
	}
};
#pragma pack(pop)
