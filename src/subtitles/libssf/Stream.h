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

#include "Exception.h"
#ifdef XY_VSFILTER_RUST_TEXT
#include "../../../include/xy_rust_ssf_stream.h"
#endif

namespace ssf
{
	class Stream
	{
	public:
		enum {EOS = -1};
		enum encoding_t {none, unknown, utf8, utf16le, utf16be, wchar};

	protected:
		int m_line, m_col;
		encoding_t m_encoding;

	public:
		Stream();
		virtual ~Stream();

		static bool IsWhiteSpace(int c, LPCWSTR morechars = NULL);

		void ThrowError(LPCTSTR fmt, ...);
	};

	class InputStream : public Stream
	{
#ifdef XY_VSFILTER_RUST_TEXT
		XyRust::SsfLookahead m_queue;
#else
		CAtlList<int> m_queue;
#endif
		int PushChar(), PopChar();

		int NextChar();

	protected:
		virtual int NextByte() = 0;

	public:
		InputStream();
		~InputStream();

		int PeekChar(), GetChar();

		int SkipWhiteSpace(LPCWSTR morechars = NULL);
	};

	class FileInputStream : public InputStream
	{
		FILE* m_file;

	protected:
		int NextByte();

	public:
		FileInputStream(const TCHAR* fn);
		~FileInputStream();
	};

	class MemoryInputStream : public InputStream
	{
		BYTE* m_pBytes;
		int m_pos, m_len;
		bool m_fFree;

	protected:
		int NextByte();

	public:
		MemoryInputStream(BYTE* pBytes, int len, bool fCopy, bool fFree);
		~MemoryInputStream();
	};

	class WCharInputStream : public InputStream
	{
		CStringW m_str;
		int m_pos;

	protected:
		int NextByte();

	public:
		WCharInputStream(CStringW str);
	};
}