// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define XY_HDMV_CDECL __cdecl
#else
#define XY_HDMV_CDECL
#endif
extern "C" {
void* XY_HDMV_CDECL xy_vsfilter_hdmv_create(int16_t, int16_t, int16_t);
void XY_HDMV_CDECL xy_vsfilter_hdmv_supply(void*, uint8_t);
int32_t XY_HDMV_CDECL xy_vsfilter_hdmv_field(const void*, uint32_t);
void XY_HDMV_CDECL xy_vsfilter_hdmv_release(void*);
}
#undef XY_HDMV_CDECL
namespace XyRust {
class HdmvDecoder {
    void* owner;
    HdmvDecoder(const HdmvDecoder&);
    HdmvDecoder& operator=(const HdmvDecoder&);
public:
    HdmvDecoder(int16_t x, int16_t y, int16_t height)
        : owner(xy_vsfilter_hdmv_create(x,y,height)) { if (!owner) abort(); }
    ~HdmvDecoder() { xy_vsfilter_hdmv_release(owner); }
    int Field(uint32_t field) const { return xy_vsfilter_hdmv_field(owner,field); }
    void Supply(uint8_t value) { xy_vsfilter_hdmv_supply(owner,value); }
};
}
