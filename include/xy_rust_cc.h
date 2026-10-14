// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_CC_CDECL __cdecl
#else
#define XY_CC_CDECL
#endif
extern "C" {
void* XY_CC_CDECL xy_vsfilter_cc_decoder_create(int32_t);
uint32_t XY_CC_CDECL xy_vsfilter_cc_decoder_next(void*, const uint8_t*, size_t);
int32_t XY_CC_CDECL xy_vsfilter_cc_decoder_field(const void*, uint32_t);
void XY_CC_CDECL xy_vsfilter_cc_decoder_release(void*);
uint32_t XY_CC_CDECL xy_vsfilter_cc_move(int32_t, int32_t, int32_t*);
uint32_t XY_CC_CDECL xy_vsfilter_cc_put(uint16_t*, int32_t, int32_t, uint16_t);
}
#undef XY_CC_CDECL
namespace XyRust {
class CaptionDecoder {
    void* owner;
    const size_t length;
    CaptionDecoder(const CaptionDecoder&);
    CaptionDecoder& operator=(const CaptionDecoder&);
public:
    explicit CaptionDecoder(int count):owner(xy_vsfilter_cc_decoder_create(count)),length(count>0?static_cast<size_t>(count):0){}
    ~CaptionDecoder(){xy_vsfilter_cc_decoder_release(owner);}
    uint32_t Next(const uint8_t* input){return xy_vsfilter_cc_decoder_next(owner,input,length);}
    int Field(uint32_t field)const{return xy_vsfilter_cc_decoder_field(owner,field);}
};
}
