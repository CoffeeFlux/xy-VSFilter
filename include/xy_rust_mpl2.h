// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_MPL2_CDECL __cdecl
#else
#define XY_MPL2_CDECL
#endif
extern "C" {
void* XY_MPL2_CDECL xy_vsfilter_mpl2_create(const uint16_t*, uint32_t);
uint32_t XY_MPL2_CDECL xy_vsfilter_mpl2_field(const void*, uint32_t, uint32_t);
void XY_MPL2_CDECL xy_vsfilter_mpl2_release(void*);
}
#undef XY_MPL2_CDECL
namespace XyRust {
class MPL2 {
    void* owner;
    MPL2(const MPL2&);
    MPL2& operator=(const MPL2&);
public:
    MPL2():owner(NULL){}
    ~MPL2(){xy_vsfilter_mpl2_release(owner);}
    template<class String> bool Convert(const String& input,String& output){
        const int length=input.GetLength();if(length<0)return false;
        std::vector<uint16_t> units(static_cast<size_t>(length));
        for(int i=0;i<length;++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_mpl2_release(owner);
        owner=xy_vsfilter_mpl2_create(units.data(),static_cast<uint32_t>(length));
        if(!owner)return false;
        const uint32_t count=xy_vsfilter_mpl2_field(owner,0,0);
        std::vector<wchar_t> converted(static_cast<size_t>(count)+1,0);
        for(uint32_t i=0;i<count;++i)converted[i]=static_cast<wchar_t>(xy_vsfilter_mpl2_field(owner,1,i));
        output=String(converted.data(),static_cast<int>(count));
        return true;
    }
};
}
