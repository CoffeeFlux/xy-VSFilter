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

#include "stdafx.h"
#include "DSUtil.h"
#include <emmintrin.h>

void memsetd(void* dst, unsigned int c, size_t nbytes)
{
	size_t n = nbytes / 4;
	size_t o = n - (n % 4);

	__m128i val = _mm_set1_epi32 ( (int)c );
	if (((uintptr_t)dst & 0x0F) == 0) { // 16-byte aligned
		for (size_t i = 0; i < o; i+=4) {
			_mm_store_si128( (__m128i*)&(((DWORD*)dst)[i]), val );
		}
	} else {
		for (size_t i = 0; i < o; i+=4) {
			_mm_storeu_si128( (__m128i*)&(((DWORD*)dst)[i]), val );
		}
	}

	switch(n - o) {
		case 3:
				((DWORD*)dst)[o + 2] = c;
		case 2:
				((DWORD*)dst)[o + 1] = c;
		case 1:
				((DWORD*)dst)[o + 0] = c;
	}
}

CString ReftimeToString(const REFERENCE_TIME& rtVal)
{
	CString		strTemp;
	LONGLONG	llTotalMs =  ConvertToMilliseconds (rtVal);
	int			lHour	  = (int)(llTotalMs  / (1000*60*60));
	int			lMinute	  = (llTotalMs / (1000*60)) % 60;
	int			lSecond	  = (llTotalMs /  1000) % 60;
	int			lMillisec = llTotalMs  %  1000;
	strTemp.Format (_T("%02d:%02d:%02d,%03d"), lHour, lMinute, lSecond, lMillisec);
	return strTemp;
}
