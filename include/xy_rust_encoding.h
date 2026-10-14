// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_ENCODING_CDECL __cdecl
#else
#define XY_ENCODING_CDECL
#endif
extern "C" {
void* XY_ENCODING_CDECL xy_vsfilter_encoding_create(const uint16_t*,uint32_t);
int32_t XY_ENCODING_CDECL xy_vsfilter_encoding_take(void*);
void XY_ENCODING_CDECL xy_vsfilter_encoding_append_unit(void*,uint16_t);
void XY_ENCODING_CDECL xy_vsfilter_encoding_append_bytes(void*,const uint8_t*,uint32_t);
uint32_t XY_ENCODING_CDECL xy_vsfilter_encoding_field(const void*,uint32_t,uint32_t);
void XY_ENCODING_CDECL xy_vsfilter_encoding_release(void*);
}
#undef XY_ENCODING_CDECL
namespace XyRust {
class Encoding {
    void* owner;
    Encoding(const Encoding&);
    Encoding& operator=(const Encoding&);
public:
    template<class String> explicit Encoding(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_encoding_create(units.data(),static_cast<uint32_t>(units.size()));
    }
    ~Encoding(){xy_vsfilter_encoding_release(owner);}
    int Take(){return xy_vsfilter_encoding_take(owner);}
    void AppendUnit(uint16_t unit){xy_vsfilter_encoding_append_unit(owner,unit);}
    void AppendBytes(const char* bytes,int count){
        xy_vsfilter_encoding_append_bytes(owner,reinterpret_cast<const uint8_t*>(bytes),count>0?static_cast<uint32_t>(count):0);
    }
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_encoding_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_encoding_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
