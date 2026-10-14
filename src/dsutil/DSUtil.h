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

#include <atlcoll.h>
//#include "MediaTypes.h"
#include "text.h"

// reference time helpers
#define MS2RT(t)        (10000i64 * (t))
#define RT2MS(t)        ((t) / 10000)
#define UNITS_FLOAT     (10000000.0)
// double version of UNITS in BaseClasses reftime

#define LCID_NOSUBTITLES -1

extern IBaseFilter* GetUpStreamFilter(IBaseFilter* pBF, IPin* pInputPin = NULL);
extern IPin* GetUpStreamPin(IBaseFilter* pBF, IPin* pInputPin = NULL);
extern IPin* GetFirstPin(IBaseFilter* pBF, PIN_DIRECTION dir = PINDIR_INPUT);
extern IBaseFilter* FindFilter(LPCWSTR clsid, IFilterGraph* pFG);
extern IBaseFilter* FindFilter(const CLSID& clsid, IFilterGraph* pFG);
extern CStringW GetFilterName(IBaseFilter* pBF);
extern CStringW GetPinName(IPin* pPin);
extern IBaseFilter* GetFilterFromPin(IPin* pPin);
extern void ShowPPage(IUnknown* pUnknown, HWND hParentWnd);
extern CLSID GetCLSID(IBaseFilter* pBF);
extern CLSID GetCLSID(IPin* pPin);
extern void memsetd(void* dst, unsigned int c, size_t nbytes);
extern bool ExtractBIH(const AM_MEDIA_TYPE* pmt, BITMAPINFOHEADER* bih);
extern bool ExtractDim(const AM_MEDIA_TYPE* pmt, int& w, int& h, int& arx, int& ary);
extern unsigned __int64 GetFileVersion(LPCTSTR fn);
extern CString CStringFromGUID(const GUID& guid);
extern CStringW UTF8To16(LPCSTR utf8);
extern CString ISO6392ToLanguage(LPCSTR code);
extern LCID    ISO6392ToLcid(LPCSTR code);
extern BOOL CFileGetStatus(LPCTSTR lpszFileName, CFileStatus& status);
extern CString ReftimeToString(const REFERENCE_TIME& rtVal);
REFERENCE_TIME StringToReftime(LPCTSTR strVal);

class CPinInfo : public PIN_INFO
{
public:
	CPinInfo() {pFilter = NULL;}
	~CPinInfo() {if(pFilter) pFilter->Release();}
};

class CFilterInfo : public FILTER_INFO
{
public:
	CFilterInfo() {pGraph = NULL;}
	~CFilterInfo() {if(pGraph) pGraph->Release();}
};

#define BeginEnumFilters(pFilterGraph, pEnumFilters, pBaseFilter) \
	{CComPtr<IEnumFilters> pEnumFilters; \
	if(pFilterGraph && SUCCEEDED(pFilterGraph->EnumFilters(&pEnumFilters))) \
	{ \
		for(CComPtr<IBaseFilter> pBaseFilter; S_OK == pEnumFilters->Next(1, &pBaseFilter, 0); pBaseFilter = NULL) \
		{ \

#define EndEnumFilters }}}

#define BeginEnumPins(pBaseFilter, pEnumPins, pPin) \
	{CComPtr<IEnumPins> pEnumPins; \
	if(pBaseFilter && SUCCEEDED(pBaseFilter->EnumPins(&pEnumPins))) \
	{ \
		for(CComPtr<IPin> pPin; S_OK == pEnumPins->Next(1, &pPin, 0); pPin = NULL) \
		{ \

#define EndEnumPins }}}

#define BeginEnumMediaTypes(pPin, pEnumMediaTypes, pMediaType) \
	{CComPtr<IEnumMediaTypes> pEnumMediaTypes; \
	if(pPin && SUCCEEDED(pPin->EnumMediaTypes(&pEnumMediaTypes))) \
	{ \
		AM_MEDIA_TYPE* pMediaType = NULL; \
		for(; S_OK == pEnumMediaTypes->Next(1, &pMediaType, NULL); DeleteMediaType(pMediaType), pMediaType = NULL) \
		{ \

#define EndEnumMediaTypes(pMediaType) } if(pMediaType) DeleteMediaType(pMediaType); }}

#define QI(i) (riid == __uuidof(i)) ? GetInterface((i*)this, ppv) :

#define countof(array) (sizeof(array)/sizeof(array[0]))

template <class T>
static CUnknown* WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT* phr)
{
	*phr = S_OK;
    CUnknown* punk = new T(lpunk, phr);
    if(punk == NULL) *phr = E_OUTOFMEMORY;
	return punk;
}
#define SAFE_DELETE(p)       { if(p) { delete (p);     (p)=NULL; } }
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=NULL; } }
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }
