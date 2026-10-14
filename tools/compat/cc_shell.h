// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "xy_rust_cc.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <tchar.h>
#else
typedef uint8_t BYTE;
typedef char16_t WCHAR;
#define __int64 int64_t
#define _T(text) L##text
enum {ANSI_CHARSET=0};
struct CPoint {int32_t x,y;CPoint(int32_t a=0,int32_t b=0):x(a),y(b){}};
class CString {
    std::wstring value;
public:
    CString(const wchar_t* s=L""):value(s){}
    explicit CString(const std::wstring& s):value(s){}
    bool IsEmpty()const{return value.empty();}
    operator const wchar_t*()const{return value.c_str();}
    int ReverseFind(wchar_t c)const{const size_t index=value.rfind(c);return index==std::wstring::npos?-1:int(index);}
    CString Left(int count)const{return CString(value.substr(0,size_t(count)));}
    CString operator+(const wchar_t* s)const{return CString(value+s);}
};
class CStringW {
    std::u16string value;
public:
    CStringW(){}
    explicit CStringW(const WCHAR* s):value(s){}
    bool IsEmpty()const{return value.empty();}
    int GetLength()const{return int(value.size());}
    WCHAR operator[](int i)const{return value[size_t(i)];}
    CStringW& operator+=(WCHAR c){value+=c;return *this;}
    CStringW& operator+=(const CStringW& s){value+=s.value;return *this;}
};
static int ccRemove(const wchar_t*){std::abort();}
static FILE* ccOpen(const wchar_t*,const wchar_t*){std::abort();}
static int ccPrint(FILE*,const wchar_t*,...){std::abort();}
#define _tremove ccRemove
#define _tfopen ccOpen
#define _ftprintf ccPrint
#endif
struct CTextFile {enum {DEFAULT_ENCODING,UTF8,LE16,BE16};};
enum {EXTSRT=0};
struct CaptionInterrupted {};
struct Caption {
    std::vector<uint16_t> text;bool unicode;int start,end;
    bool operator==(const Caption& o)const{return text==o.text && unicode==o.unicode && start==o.start && end==o.end;}
};
static void (*ccAddHook)(const Caption&)=nullptr;
class CSimpleTextSubtitle {
public:
    std::vector<Caption> entries;
    void CreateDefaultStyle(int){}
    bool IsEmpty()const{return entries.empty();}
    void Add(const CStringW& text,bool unicode,int start,int end){
        Caption entry={{},unicode,start,end};for(int i=0;i<text.GetLength();++i)entry.text.push_back(uint16_t(text[i]));
        if(ccAddHook)ccAddHook(entry);entries.push_back(entry);
    }
    void Sort(){std::abort();}
    void SaveAs(const CString&,int,int,int){std::abort();}
};

static uint64_t ccTextHandled=0,ccTextFallback=0;
namespace XyRust {
class ObservedCaptionText:public CaptionText {
public:
    explicit ObservedCaptionText(const uint16_t* cells):CaptionText(cells){}
    bool Handled()const{const bool handled=CaptionText::Handled();if(handled)++ccTextHandled;else ++ccTextFallback;return handled;}
};
}
#define CaptionText ObservedCaptionText
