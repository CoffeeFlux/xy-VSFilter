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
void* XY_CC_CDECL xy_vsfilter_cc_packet_create(const uint8_t*, int32_t);
int32_t XY_CC_CDECL xy_vsfilter_cc_packet_field(const void*, uint32_t);
void XY_CC_CDECL xy_vsfilter_cc_packet_copy(const void*, uint8_t*, uint8_t*);
void XY_CC_CDECL xy_vsfilter_cc_packet_release(void*);
void* XY_CC_CDECL xy_vsfilter_cc_text_create(const uint16_t*);
size_t XY_CC_CDECL xy_vsfilter_cc_text_field(const void*, uint32_t, size_t);
void XY_CC_CDECL xy_vsfilter_cc_text_release(void*);
void* XY_CC_CDECL xy_vsfilter_cc_decoder_create(int32_t);
uint32_t XY_CC_CDECL xy_vsfilter_cc_decoder_next(void*, const uint8_t*, size_t);
int32_t XY_CC_CDECL xy_vsfilter_cc_decoder_field(const void*, uint32_t);
void XY_CC_CDECL xy_vsfilter_cc_decoder_release(void*);
uint32_t XY_CC_CDECL xy_vsfilter_cc_move(int32_t, int32_t, int32_t*);
uint32_t XY_CC_CDECL xy_vsfilter_cc_put(uint16_t*, int32_t, int32_t, uint16_t);
}
#undef XY_CC_CDECL
namespace XyRust {
class CaptionPacket {
    void* owner;
    CaptionPacket(const CaptionPacket&);
    CaptionPacket& operator=(const CaptionPacket&);
public:
    CaptionPacket(const uint8_t* input,int length):owner(xy_vsfilter_cc_packet_create(input,length)){}
    ~CaptionPacket(){xy_vsfilter_cc_packet_release(owner);}
    bool Handled()const{return owner!=NULL;}
    int Position()const{return xy_vsfilter_cc_packet_field(owner,0);}
    int Length(unsigned field)const{return xy_vsfilter_cc_packet_field(owner,field+2);}
    void Copy(uint8_t* first,uint8_t* second)const{xy_vsfilter_cc_packet_copy(owner,first,second);}
};
class CaptionText {
    void* owner;
    CaptionText(const CaptionText&);
    CaptionText& operator=(const CaptionText&);
public:
    explicit CaptionText(const uint16_t* cells):owner(xy_vsfilter_cc_text_create(cells)){}
    ~CaptionText(){xy_vsfilter_cc_text_release(owner);}
    bool Handled()const{return xy_vsfilter_cc_text_field(owner,0,0)!=0;}
    size_t Length()const{return xy_vsfilter_cc_text_field(owner,1,0);}
    uint16_t Unit(size_t index)const{return static_cast<uint16_t>(xy_vsfilter_cc_text_field(owner,2,index));}
};
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
