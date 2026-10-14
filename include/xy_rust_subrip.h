// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_SUBRIP_CDECL __cdecl
#else
#define XY_SUBRIP_CDECL
#endif
extern "C" {
void* XY_SUBRIP_CDECL xy_vsfilter_subrip_create(const uint16_t*, uint32_t);
uint32_t XY_SUBRIP_CDECL xy_vsfilter_subrip_field(const void*, uint32_t, uint32_t);
void XY_SUBRIP_CDECL xy_vsfilter_subrip_release(void*);
}
#undef XY_SUBRIP_CDECL
namespace XyRust {
class SubRip {
    void* owner;
    SubRip(const SubRip&);
    SubRip& operator=(const SubRip&);
public:
    SubRip():owner(NULL){}
    ~SubRip(){xy_vsfilter_subrip_release(owner);}
    template<class String> bool Convert(const String& input,String& output){
        const int length=input.GetLength();if(length<0)return false;
        std::vector<uint16_t> units(static_cast<size_t>(length));
        for(int i=0;i<length;++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_subrip_release(owner);
        owner=xy_vsfilter_subrip_create(units.data(),static_cast<uint32_t>(length));
        if(!owner)return false;
        const uint32_t count=xy_vsfilter_subrip_field(owner,0,0);
        std::vector<wchar_t> converted(static_cast<size_t>(count)+1,0);
        for(uint32_t i=0;i<count;++i)converted[i]=static_cast<wchar_t>(xy_vsfilter_subrip_field(owner,1,i));
        output=String(converted.data(),static_cast<int>(count));
        return true;
    }
};
}
