// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>
#include "xy_rust_webvtt.h"
#ifdef _WIN32
#include <atlstr.h>
#endif
#ifndef _T
#define _T(value) L##value
#endif
struct CueInterrupted {};
static int cueFailure=-1,cueAttempts=0;
static bool cueMutate=false,cueMutated=false;
static std::vector<std::vector<uint16_t>> cueEvents;
class CueString {
#ifdef _WIN32
    ATL::CStringW value;
#else
    std::wstring value;
#endif
    void observe(uint16_t action){
        std::vector<uint16_t> event={action};for(int i=0;i<GetLength();++i)event.push_back(static_cast<uint16_t>((*this)[i]));cueEvents.push_back(event);
    }
public:
    CueString(){}
    CueString(const wchar_t* text,int length)
#ifdef _WIN32
        :value(text,length){}
#else
        :value(text,static_cast<size_t>(length)){}
#endif
    int GetLength()const{
#ifdef _WIN32
        return value.GetLength();
#else
        return static_cast<int>(value.size());
#endif
    }
    wchar_t operator[](size_t i)const{
#ifdef _WIN32
        return value[static_cast<int>(i)];
#else
        return value.at(i);
#endif
    }
    int Find(wchar_t c)const{
#ifdef _WIN32
        return value.Find(c);
#else
        const auto* p=std::wcschr(value.c_str(),c);return p?static_cast<int>(p-value.c_str()):-1;
#endif
    }
    CueString Left(int count)const{
#ifdef _WIN32
        auto result=value.Left(count);return CueString(result.GetString(),result.GetLength());
#else
        auto result=value.substr(0,static_cast<size_t>(count));return CueString(result.data(),static_cast<int>(result.size()));
#endif
    }
    bool operator==(const wchar_t* other)const{
#ifdef _WIN32
        return value==other;
#else
        return std::wcscmp(value.c_str(),other)==0;
#endif
    }
    void Delete(int start,int count){
        observe(1);if(cueAttempts++==cueFailure)throw CueInterrupted();
#ifdef _WIN32
        value.Delete(start,count);
#else
        value.erase(static_cast<size_t>(start),static_cast<size_t>(count));
#endif
        if(cueMutate){value=L"\n \tchanged after deletion";cueMutated=true;}observe(2);
    }
    void TrimLeft(){
        observe(3);if(cueAttempts++==cueFailure)throw CueInterrupted();
#ifdef _WIN32
        value.TrimLeft();
#else
        size_t pos=0;while(pos<value.size() && value[pos] && std::iswspace(value[pos]))++pos;value.erase(0,pos);
#endif
        observe(4);
    }
};
#define CStringW CueString
