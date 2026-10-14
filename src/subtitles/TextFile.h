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

#include <afx.h>
#include <vector>

#include "StdioFile64.h"

class CTextFile : protected CStdioFile64
{
public:
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

	// Set by OpenMemory(): read from this buffer instead of the file.
	bool m_inMemory;
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

public:
	CTextFile(enc e = DEFAULT_ENCODING);

	virtual bool Open(LPCTSTR lpszFileName);
	virtual bool Save(LPCTSTR lpszFileName, enc e /*= DEFAULT_ENCODING*/);

	// Reads from a caller-owned buffer, which must outlive this object.
	// GetFilePath() returns an empty string for in-memory data.
	bool OpenMemory(const void* pData, size_t len);
	// Returns the buffer passed to OpenMemory(), or NULL when reading a file.
	const BYTE* GetMemoryData(size_t& len) const;

	void SetEncoding(enc e);
	enc GetEncoding();
	bool IsUnicode();

	// CFile
	CString GetFilePath() const;

	// CStdioFile

    ULONGLONG GetPosition() const;
	ULONGLONG GetLength() const;
	ULONGLONG Seek(LONGLONG lOff, UINT nFrom);

	void WriteString(LPCWSTR lpsz/*CStringW str*/);
	BOOL ReadString(CStringW& str);
};

class CWebTextFile : public CTextFile
{
	LONGLONG m_llMaxSize;
	std::vector<BYTE> m_data;

public:
    CWebTextFile(CTextFile::enc e = DEFAULT_ENCODING, LONGLONG llMaxSize = 1024 * 1024);

	// http:// URLs are downloaded into memory; anything else is opened as a file.
	bool Open(LPCTSTR lpszFileName);
	bool Save(LPCTSTR lpszFileName, enc e /*= DEFAULT_ENCODING*/);
};
