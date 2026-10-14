// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_SSF_LITERAL_TEXT_CDECL __cdecl
#else
#define XY_SSF_LITERAL_TEXT_CDECL
#endif
extern "C" {
void* XY_SSF_LITERAL_TEXT_CDECL xy_vsfilter_ssf_literal_text_create();
uint32_t XY_SSF_LITERAL_TEXT_CDECL xy_vsfilter_ssf_literal_text_append(void*,uint16_t);
int32_t XY_SSF_LITERAL_TEXT_CDECL xy_vsfilter_ssf_literal_text_length(const void*);
const uint16_t* XY_SSF_LITERAL_TEXT_CDECL xy_vsfilter_ssf_literal_text_data(const void*);
void XY_SSF_LITERAL_TEXT_CDECL xy_vsfilter_ssf_literal_text_release(void*);
}
#undef XY_SSF_LITERAL_TEXT_CDECL
namespace XyRust {
class SsfLiteralText {
    void* owner;
    CStringW fallback;
    SsfLiteralText(const SsfLiteralText&);
    SsfLiteralText& operator=(const SsfLiteralText&);
public:
    SsfLiteralText():owner(xy_vsfilter_ssf_literal_text_create()){}
    ~SsfLiteralText(){xy_vsfilter_ssf_literal_text_release(owner);}
    operator CStringW()const{
        if(!owner)return fallback;
        const int length=xy_vsfilter_ssf_literal_text_length(owner);
        if(!length)return CStringW();
        return CStringW(reinterpret_cast<const WCHAR*>(xy_vsfilter_ssf_literal_text_data(owner)),length);
    }
    void operator+=(WCHAR value){
        if(owner){
            if(xy_vsfilter_ssf_literal_text_append(owner,static_cast<uint16_t>(value)))return;
            // Copy every initialized unit before ending the Rust owner. An
            // interrupted native copy leaves that owner available for cleanup.
            fallback=static_cast<CStringW>(*this);
            xy_vsfilter_ssf_literal_text_release(owner);
            owner=NULL;
        }
        fallback+=value;
    }
};
}
