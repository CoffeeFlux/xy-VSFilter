// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_WRITER_CDECL __cdecl
#else
#define XY_WRITER_CDECL
#endif
extern "C" {
void* XY_WRITER_CDECL xy_vsfilter_writer_create(const uint16_t*,uint32_t,uint32_t);
uint32_t XY_WRITER_CDECL xy_vsfilter_writer_field(const void*,uint32_t,uint32_t);
const unsigned char* XY_WRITER_CDECL xy_vsfilter_writer_data(const void*);
void XY_WRITER_CDECL xy_vsfilter_writer_release(void*);
}
#undef XY_WRITER_CDECL
namespace XyRust {
class TextWriter {
    void* owner;
    TextWriter(const TextWriter&);
    TextWriter& operator=(const TextWriter&);
public:
    template<class String> TextWriter(const String& input,bool bigEndian):owner(NULL){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_writer_create(units.data(),static_cast<uint32_t>(units.size()),bigEndian?1:0);
    }
    ~TextWriter(){xy_vsfilter_writer_release(owner);}
    uint32_t Size()const{return xy_vsfilter_writer_field(owner,2,0);}
    // Immutable bytes loaned to native Write; this owner outlives that call.
    const unsigned char* Data()const{return xy_vsfilter_writer_data(owner);}
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_writer_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_writer_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
