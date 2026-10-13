// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define XY_DVB_CDECL __cdecl
#else
#define XY_DVB_CDECL
#endif
extern "C" {
void* XY_DVB_CDECL xy_vsfilter_dvb_create(uint32_t);
void XY_DVB_CDECL xy_vsfilter_dvb_supply(void*, uint8_t);
int32_t XY_DVB_CDECL xy_vsfilter_dvb_field(const void*, uint32_t);
void XY_DVB_CDECL xy_vsfilter_dvb_release(void*);
}
#undef XY_DVB_CDECL
namespace XyRust {
class DvbDecoder {
    void* owner;
    DvbDecoder(const DvbDecoder&);
    DvbDecoder& operator=(const DvbDecoder&);
public:
    explicit DvbDecoder(uint32_t bits) : owner(xy_vsfilter_dvb_create(bits)) { if (!owner) abort(); }
    ~DvbDecoder() { xy_vsfilter_dvb_release(owner); }
    int Field(uint32_t field) const { return xy_vsfilter_dvb_field(owner,field); }
    void Supply(uint8_t value) { xy_vsfilter_dvb_supply(owner,value); }
    template<typename Buffer> bool Next(Buffer& buffer) {
        for (;;) {
            switch (Field(0)) {
            case 0: return false;
            case 1: Supply(buffer.IsEOF() ? 1 : 0); break;
            case 2: Supply(static_cast<uint8_t>(buffer.BitRead(Field(1)))); break;
            case 3: Supply(buffer.ReadByte()); break;
            case 4: return true;
            default: abort();
            }
        }
    }
};
}
