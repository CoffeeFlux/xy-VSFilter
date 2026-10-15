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

#include "xy_atl.h"
#include <stdio.h>
#include <vector>

// Reads text line by line from a memory buffer. The encoding comes
// from the BOM (UTF-8, UTF-16LE/BE); without one the default encoding is used,
// and a line that isn't valid UTF-8 is re-read as one character per byte.
class CTextFile
{
public:
    enum {begin = SEEK_SET, current = SEEK_CUR, end = SEEK_END};

    enum enc {
        DEFAULT_ENCODING,
        UTF8,
        LE16,
        BE16
    };

private:
	enc m_encoding, m_defaultencoding;
	int m_offset;

	unsigned char m_readbuffer[4096];
	size_t m_bufferPos;
	size_t m_bufferCount;
	std::vector<wchar_t> m_convertedBuffer;

	enum charerror {CHARERR_NEED_MORE=-1, CHARERR_REOPEN=-2};

	const BYTE* m_memData;
	size_t m_memLen;
	size_t m_memPos;

	int NextChar();
	bool ReadLine();
	bool DetectEncoding();

	UINT RawRead(void* lpBuf, UINT nCount);
	ULONGLONG RawGetPosition() const;
	ULONGLONG RawGetLength() const;
	ULONGLONG RawSeek(ULONGLONG pos);

	CTextFile(const CTextFile&);
	CTextFile& operator=(const CTextFile&);

public:
	CTextFile(enc e = DEFAULT_ENCODING);

	// Reads from a caller-owned buffer, which must outlive this object.
	bool OpenMemory(const void* pData, size_t len);

	enc GetEncoding();
	bool IsUnicode();

    ULONGLONG GetPosition() const;
	ULONGLONG GetLength() const;
	ULONGLONG Seek(LONGLONG lOff, UINT nFrom);

	BOOL ReadString(CStringW& str);
};

