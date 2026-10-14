// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ass_style_shell.h"
#include "xy_rust_microdvd_style.h"
#undef swscanf
#undef swscanf_s
#undef min
#undef max
static uint64_t microStyleOwners=0,microStyleSteps=0,microStyleScans=0;
namespace XyRust {
class ObservedMicroDvdStyle : public MicroDvdStyle {
public:
    template<class String> explicit ObservedMicroDvdStyle(const String& input):MicroDvdStyle(input){++microStyleOwners;}
    bool Next(){++microStyleSteps;return MicroDvdStyle::Next();}
};
}
static int microStylePrefix(const wchar_t* text,const wchar_t* prefix,size_t length){
    microObserve(41,text);searchEvents.push_back(length);
#ifdef _WIN32
    const int result=::_wcsnicmp(text,prefix,length);
#else
    const int result=::wcsncasecmp(text,prefix,length);
#endif
    searchEvents.push_back(static_cast<uint32_t>(result));return result;
}
static wint_t microStyleLower(wint_t unit){
    searchObserve(42,static_cast<uint32_t>(unit));const wint_t result=std::towlower(unit);
    searchEvents.push_back(static_cast<uint32_t>(result));return result;
}
template<class T> static int microStyleNumeric(const wchar_t* text,const wchar_t* format,T* value){
    ++microStyleScans;microObserve(43,text);
#ifdef _WIN32
    const int result=::swscanf_s(text,format,value);
#else
    const int result=std::swscanf(text,format,value);
#endif
    if(std::wcscmp(format,L"{p:%d")==0 && result!=1){fprintf(stderr,"Position fixture outside successful numeric-scan scope\n");std::abort();}
    searchEvents.push_back(static_cast<uint32_t>(result));
    if(result==1){static_assert(sizeof(T)==4,"Native numeric field must be 32-bit");uint32_t bits;std::memcpy(&bits,value,4);searchEvents.push_back(bits);}
    searchEvents.push_back(errno);return result;
}
#ifdef _tcsnicmp
#undef _tcsnicmp
#endif
#ifdef _stscanf_s
#undef _stscanf_s
#endif
#define _tcsnicmp microStylePrefix
#define _stscanf_s microStyleNumeric
#define towlower microStyleLower
