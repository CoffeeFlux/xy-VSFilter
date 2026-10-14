// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_FONT_RECORDS_CDECL __cdecl
#else
#define XY_FONT_RECORDS_CDECL
#endif
extern "C" {
void* XY_FONT_RECORDS_CDECL xy_vsfilter_font_records_create();
uint32_t XY_FONT_RECORDS_CDECL xy_vsfilter_font_records_classify(const uint16_t*,uint32_t);
void XY_FONT_RECORDS_CDECL xy_vsfilter_font_records_append(void*,const uint16_t*,uint32_t);
void XY_FONT_RECORDS_CDECL xy_vsfilter_font_records_clear(void*);
uint32_t XY_FONT_RECORDS_CDECL xy_vsfilter_font_records_field(const void*,uint32_t,uint32_t);
void XY_FONT_RECORDS_CDECL xy_vsfilter_font_records_release(void*);
}
#undef XY_FONT_RECORDS_CDECL
namespace XyRust {
class FontRecords {
    void* owner;
    FontRecords(const FontRecords&);
    FontRecords& operator=(const FontRecords&);
public:
    FontRecords():owner(xy_vsfilter_font_records_create()){}
    ~FontRecords(){xy_vsfilter_font_records_release(owner);}
    template<class String> uint32_t Classify(const String& input)const{
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        return xy_vsfilter_font_records_classify(units.data(),static_cast<uint32_t>(units.size()));
    }
    template<class String> void Append(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_font_records_append(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    void Clear(){xy_vsfilter_font_records_clear(owner);}
    bool Empty()const{return xy_vsfilter_font_records_field(owner,0,0)==0;}
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_font_records_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_font_records_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
