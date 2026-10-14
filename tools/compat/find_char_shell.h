// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>
#include "xy_rust_find_char.h"
#ifdef _WIN32
#include <windows.h>
#include <atlstr.h>
#include <tchar.h>
typedef ATL::CStringW SearchString;
#else
typedef uint32_t DWORD;
typedef uintptr_t DWORD_PTR;
typedef unsigned char BYTE;
typedef wchar_t WCHAR;
struct CHARSETINFO { DWORD ciACP; };
#define TCI_SRCCHARSET 1
class SearchString {
    std::wstring text;
public:
    SearchString(const wchar_t* p,int n):text(p,static_cast<size_t>(n)){}
    int GetLength()const{return static_cast<int>(text.size());}
    wchar_t operator[](int i)const{if(i==GetLength())return 0;return text.at(static_cast<size_t>(i));}
    int Find(wchar_t c,int pos)const{
        if(pos<0 || pos>=GetLength())return -1;
        const wchar_t* found=std::wcschr(text.c_str()+pos,c);
        return found?static_cast<int>(found-text.c_str()):-1;
    }
    SearchString Mid(int start,int count)const{auto s=text.substr(static_cast<size_t>(start),static_cast<size_t>(count));return SearchString(s.data(),static_cast<int>(s.size()));}
    bool operator==(const wchar_t* p)const{return std::wcscmp(text.c_str(),p)==0;}
};
#endif
static uint64_t rustSearches=0,rustSearchSteps=0,rustUnicodeSearches=0;
namespace XyRust {
template<class String> int ObservedUnicode(const String& input,uint16_t target,int position){++rustUnicodeSearches;return FindUnicode(input,target,position);}

class ObservedSearch : public FindChar {
public:
    ObservedSearch(uint16_t target,int position,int length):FindChar(target,position,length){++rustSearches;}
    template<class String> uint32_t Step(const String& input,bool reply){++rustSearchSteps;return FindChar::Step(input,reply);}
};
}
struct SearchInterrupted {};
static int searchFailure=-1,searchAttempts=0,searchMode=0;
static std::vector<uint64_t> searchEvents;
static void searchObserve(unsigned kind,uint32_t a,uint32_t b=0){
    searchEvents.push_back((uint64_t(kind)<<56)|(uint64_t(a)<<24)|b);
    if(searchAttempts++==searchFailure)throw SearchInterrupted();
}
static int observedTranslate(DWORD* source,CHARSETINFO* result,DWORD flags){
    const DWORD charset=static_cast<DWORD>(reinterpret_cast<uintptr_t>(source));
    searchObserve(1,charset,flags);
#ifdef _WIN32
    return ::TranslateCharsetInfo(source,result,flags);
#else
    switch(charset){
    case 0: case 1: result->ciACP=1252;break;
    case 128:result->ciACP=932;break;
    case 129:result->ciACP=949;break;
    case 130:result->ciACP=1361;break;
    case 134:result->ciACP=936;break;
    case 136:result->ciACP=950;break;
    default:result->ciACP=0;return 0;
    }
    return 1;
#endif
}
static int observedLead(DWORD cp,BYTE byte){
    searchObserve(2,cp,byte);
    if(searchMode==1)return byte=='{' || byte=='e' || byte=='|';
    if(searchMode==2)return (searchAttempts%3)==0;
#ifdef _WIN32
    return ::IsDBCSLeadByteEx(cp,byte);
#else
    if(cp==932)return (byte>=0x81 && byte<=0x9f)||(byte>=0xe0 && byte<=0xfc);
    if(cp==936 || cp==949 || cp==950)return byte>=0x81 && byte<=0xfe;
    if(cp==1361)return (byte>=0x84 && byte<=0xd3)||(byte>=0xd8 && byte<=0xde)||(byte>=0xe0 && byte<=0xf9);
    return 0;
#endif
}
static int observedDigit(WCHAR unit){
    searchObserve(3,static_cast<uint32_t>(unit));
#ifdef _WIN32
    return _istdigit(unit);
#else
    return std::iswdigit(unit);
#endif
}
#define CStringW SearchString
#define TranslateCharsetInfo observedTranslate
#define IsDBCSLeadByteEx observedLead
#ifdef _istdigit
#undef _istdigit
#endif
#define _istdigit observedDigit
