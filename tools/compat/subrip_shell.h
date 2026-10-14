// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "xy_rust_subrip.h"
#ifdef _WIN32
#include <atlstr.h>
typedef ATL::CStringW SubRipString;
#else
class SubRipString {
    std::wstring value;
public:
    SubRipString(){}
    SubRipString(const wchar_t* input,int length):value(input,static_cast<size_t>(length)){}
    int GetLength()const{return static_cast<int>(value.size());}
    wchar_t operator[](size_t i)const{return value.at(i);}
    int Replace(const wchar_t* from,const wchar_t* to){
        const std::wstring needle(from),replacement(to);size_t pos=0;int count=0;
        while((pos=value.find(needle,pos))!=std::wstring::npos){value.replace(pos,needle.size(),replacement);pos+=replacement.size();++count;}
        return count;
    }
};
#endif
#define CStringW SubRipString
