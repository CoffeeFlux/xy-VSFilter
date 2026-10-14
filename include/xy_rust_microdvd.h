// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_MICRODVD_CDECL __cdecl
#else
#define XY_MICRODVD_CDECL
#endif
extern "C" {
void* XY_MICRODVD_CDECL xy_vsfilter_microdvd_create(int32_t);
uint32_t XY_MICRODVD_CDECL xy_vsfilter_microdvd_step(void*,int32_t);
void XY_MICRODVD_CDECL xy_vsfilter_microdvd_append(void*,const uint16_t*,uint32_t,uint32_t);
int32_t XY_MICRODVD_CDECL xy_vsfilter_microdvd_field(const void*,uint32_t,uint32_t);
void XY_MICRODVD_CDECL xy_vsfilter_microdvd_release(void*);
}
#undef XY_MICRODVD_CDECL
namespace XyRust {
class MicroDVD {
    void* owner;
    MicroDVD(const MicroDVD&);
    MicroDVD& operator=(const MicroDVD&);
public:
    explicit MicroDVD(int length):owner(xy_vsfilter_microdvd_create(length)){}
    ~MicroDVD(){xy_vsfilter_microdvd_release(owner);}
    unsigned Step(int reply){return xy_vsfilter_microdvd_step(owner,reply);}
    int Field(unsigned field)const{return xy_vsfilter_microdvd_field(owner,field,0);}
    template<class String> void Append(const String& input,unsigned restore){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_microdvd_append(owner,units.data(),static_cast<uint32_t>(units.size()),restore);
    }
    template<class String> String Output()const{
        const int count=Field(3);
        std::vector<wchar_t> units(static_cast<size_t>(count)+1,0);
        for(int i=0;i<count;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_microdvd_field(owner,4,static_cast<uint32_t>(i)));
        return String(units.data(),count);
    }
};
}
