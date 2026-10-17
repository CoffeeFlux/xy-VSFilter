// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <cstring>
#ifdef _MSC_VER
#define XY_ASS_STYLE_CDECL __cdecl
#else
#define XY_ASS_STYLE_CDECL
#endif
extern "C" {
void* XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_create(const uint16_t*,uint32_t,int32_t);
uint32_t XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_next(void*);
void XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_accept(void*,const uint16_t*,uint32_t);
void XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_normalize(void*,const uint32_t*,int32_t,int32_t,int32_t,int32_t);
uint32_t XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_field(const void*,uint32_t,uint32_t);
void XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_release(void*);
uint32_t XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_integer(void*,uint32_t,int32_t);
uint32_t XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_float_bits(void*,uint32_t,uint64_t);
uint32_t XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_text(void*,uint32_t,const uint16_t*,uint32_t);
uint32_t XY_ASS_STYLE_CDECL xy_vsfilter_ass_style_finish(void*);
}
#undef XY_ASS_STYLE_CDECL
namespace XyRust {
class AssStyle {
    void* owner;
    AssStyle(const AssStyle&);
    AssStyle& operator=(const AssStyle&);
public:
    template<class String> AssStyle(const String& input,int version){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_ass_style_create(units.data(),static_cast<uint32_t>(units.size()),version);
    }
    ~AssStyle(){xy_vsfilter_ass_style_release(owner);}
    uint32_t Next(){return xy_vsfilter_ass_style_next(owner);}
    template<class String> void Accept(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_ass_style_accept(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    void Normalize(const uint32_t* colors,int alpha,int encoding,int border,int alignment){xy_vsfilter_ass_style_normalize(owner,colors,alpha,encoding,border,alignment);}
    uint32_t Number(uint32_t index)const{return xy_vsfilter_ass_style_field(owner,2,index);}
    void Integer(uint32_t field,int value) {if(!xy_vsfilter_ass_style_integer(owner,field,value))throw 1;}
    void Real(uint32_t field,double value) {
        uint64_t bits;static_assert(sizeof bits==sizeof value,"native double width");std::memcpy(&bits,&value,sizeof bits);
        if(!xy_vsfilter_ass_style_float_bits(owner,field,bits))throw 1;
    }
    template<class String> void StringValue(uint32_t field,const String& value) {
        std::vector<uint16_t> units(static_cast<size_t>(value.GetLength()));
        for(int i=0;i<value.GetLength();++i)units[i]=static_cast<uint16_t>(value[i]);
        if(!xy_vsfilter_ass_style_text(owner,field,units.data(),static_cast<uint32_t>(units.size())))throw 1;
    }
    std::vector<uint8_t> Finish() {
        if(!xy_vsfilter_ass_style_finish(owner))throw 1;
        std::vector<uint8_t> result(xy_vsfilter_ass_style_field(owner,3,0));
        for(size_t i=0;i<result.size();++i)result[i]=static_cast<uint8_t>(xy_vsfilter_ass_style_field(owner,4,static_cast<uint32_t>(i)));
        return result;
    }
    template<class String> String Name()const {
        const uint32_t length=xy_vsfilter_ass_style_field(owner,5,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_ass_style_field(owner,6,i));
        return String(units.data(),static_cast<int>(length));
    }
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_ass_style_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_ass_style_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
