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
void* XY_DVB_CDECL xy_vsfilter_dvb_field_create(int32_t, int16_t, int16_t, int16_t);
void XY_DVB_CDECL xy_vsfilter_dvb_field_supply(void*, int32_t, int16_t, int16_t);
int32_t XY_DVB_CDECL xy_vsfilter_dvb_field_query(const void*, uint32_t);
void XY_DVB_CDECL xy_vsfilter_dvb_field_release(void*);
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

namespace XyRust {
class DvbFieldDecoder {
    void* owner;
    DvbFieldDecoder(const DvbFieldDecoder&);
    DvbFieldDecoder& operator=(const DvbFieldDecoder&);
public:
    DvbFieldDecoder(int position, short length, short x, short y)
        : owner(xy_vsfilter_dvb_field_create(position,length,x,y)) { if (!owner) abort(); }
    ~DvbFieldDecoder() { xy_vsfilter_dvb_field_release(owner); }
    int Field(uint32_t field) const { return xy_vsfilter_dvb_field_query(owner,field); }
    void Supply(int value, short x=0, short y=0) { xy_vsfilter_dvb_field_supply(owner,value,x,y); }
    template<typename Buffer> int Next(Buffer& buffer) {
        for (;;) {
            switch (Field(0)) {
            case 0: return 0;
            case 1: Supply(buffer.GetPos()); break;
            case 2: Supply(buffer.ReadByte()); break;
            case 3: return Field(1);
            case 4: buffer.SkipBytes(Field(1)); Supply(0); break;
            case 5: return -1;
            default: abort();
            }
        }
    }
};
}
