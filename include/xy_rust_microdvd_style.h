// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_MICRODVD_STYLE_CDECL __cdecl
#else
#define XY_MICRODVD_STYLE_CDECL
#endif
extern "C" {
void* XY_MICRODVD_STYLE_CDECL xy_vsfilter_microdvd_style_create(const uint16_t*,uint32_t);
uint32_t XY_MICRODVD_STYLE_CDECL xy_vsfilter_microdvd_style_next(void*);
uint32_t XY_MICRODVD_STYLE_CDECL xy_vsfilter_microdvd_style_field(const void*,uint32_t,uint32_t);
void XY_MICRODVD_STYLE_CDECL xy_vsfilter_microdvd_style_release(void*);
}
#undef XY_MICRODVD_STYLE_CDECL
namespace XyRust {
class MicroDvdStyle {
    void* owner;
    MicroDvdStyle(const MicroDvdStyle&);
    MicroDvdStyle& operator=(const MicroDvdStyle&);
public:
    template<class String> explicit MicroDvdStyle(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_microdvd_style_create(units.data(),static_cast<uint32_t>(units.size()));
    }
    ~MicroDvdStyle(){xy_vsfilter_microdvd_style_release(owner);}
    bool Next(){return xy_vsfilter_microdvd_style_next(owner)!=0;}
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_microdvd_style_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_microdvd_style_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
