// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef _WIN32
#include <atlstr.h>
#include <atlcoll.h>
typedef ATL::CStringW SamiNativeString;
typedef ATL::CAtlMap<SamiNativeString,DWORD,ATL::CStringElementTraits<SamiNativeString>> SamiNativeColors;
#endif
#include "microdvd_shell.h"
#include "xy_rust_sami.h"
#include <map>
#include <cerrno>
#ifndef _T
#define _T(value) L##value
#endif
class SamiColors {
#ifdef _WIN32
    SamiNativeColors colors;
#else
    std::map<std::wstring,DWORD> colors;
#endif
public:
    template<class Entry> SamiColors(const Entry* entries,size_t count){
        for(size_t i=0;i<count;++i){
#ifdef _WIN32
            colors.SetAt(entries[i].name,entries[i].color);
#else
            colors[entries[i].name]=entries[i].color;
#endif
        }
    }
    bool Lookup(const MicroString& key,DWORD& color){
        microObserve(7,key.GetString());
#ifdef _WIN32
        return colors.Lookup(SamiNativeString(key.GetString(),key.GetLength()),color);
#else
        auto found=colors.find(std::wstring(key.GetString(),static_cast<size_t>(key.GetLength())));
        if(found==colors.end())return false;color=found->second;return true;
#endif
    }
};
static long samiWcstol(const wchar_t* text,wchar_t** end,int radix){microObserve(8,text);return std::wcstol(text,end,radix);}
static uint64_t samiHandled=0,samiFallbacks=0,samiSteps=0,samiInsertions=0;
namespace XyRust {
class ObservedSami : public Sami {
public:
    template<class String> ObservedSami(const String& text,const String& lower):Sami(text,lower){if(Handled())++samiHandled;else ++samiFallbacks;}
    unsigned Step(){++samiSteps;return Sami::Step();}
    template<class String> void Insert(const String& input,int advance){++samiInsertions;Sami::Insert(input,advance);}
};
}
#define wcstol samiWcstol
#define CString MicroString
