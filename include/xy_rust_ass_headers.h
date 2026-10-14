// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_ASS_HEADERS_CDECL __cdecl
#else
#define XY_ASS_HEADERS_CDECL
#endif
extern "C" {
void* XY_ASS_HEADERS_CDECL xy_vsfilter_ass_headers_create();
uint32_t XY_ASS_HEADERS_CDECL xy_vsfilter_ass_headers_classify(void*,const uint16_t*,uint32_t);
void XY_ASS_HEADERS_CDECL xy_vsfilter_ass_headers_version(void*,int32_t);
int32_t XY_ASS_HEADERS_CDECL xy_vsfilter_ass_headers_field(const void*,uint32_t);
uint32_t XY_ASS_HEADERS_CDECL xy_vsfilter_ass_header_value(const uint16_t*,uint32_t,uint32_t);
void XY_ASS_HEADERS_CDECL xy_vsfilter_ass_headers_release(void*);
}
#undef XY_ASS_HEADERS_CDECL
namespace XyRust {
class AssHeaders {
    void* owner;
    AssHeaders(const AssHeaders&);
    AssHeaders& operator=(const AssHeaders&);
public:
    AssHeaders():owner(xy_vsfilter_ass_headers_create()){}
    ~AssHeaders(){xy_vsfilter_ass_headers_release(owner);}
    template<class String> uint32_t Classify(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        return xy_vsfilter_ass_headers_classify(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    template<class String> uint32_t Value(const String& input,uint32_t mode)const{
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        return xy_vsfilter_ass_header_value(units.data(),static_cast<uint32_t>(units.size()),mode);
    }
    void ScriptVersion(int version){xy_vsfilter_ass_headers_version(owner,version);}
    int Field(uint32_t field)const{return xy_vsfilter_ass_headers_field(owner,field);}
};
}
