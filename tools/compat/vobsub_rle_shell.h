// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "vobsub_trim_shell.h"
using std::min;
typedef uint8_t BYTE;
struct RleInterrupted {};
static std::vector<uint64_t> rleEvents;
static int rleFailure=-1,rleObservations=0;
static uint64_t nibbleCalls=0,codeCalls=0;
template<class Image> static void observeRle(const Image& image,unsigned action,int x=0,int y=0,int length=0,size_t color=0) {
    for(uint64_t value:{uint64_t(action),uint64_t(static_cast<uint32_t>(x)),uint64_t(static_cast<uint32_t>(y)),uint64_t(static_cast<uint32_t>(length)),uint64_t(color),uint64_t(image.nOffset[0]),uint64_t(image.nOffset[1]),uint64_t(image.nPlane),uint64_t(image.fAligned),uint64_t(static_cast<uint32_t>(image.rect.bottom))})rleEvents.push_back(value);
    // Exact snapshots at every boundary, including intermediate overwritten pixels.
    for(size_t i=0;i<image.backingCount;++i) {
        const auto& c=image.backing[i];rleEvents.push_back(uint32_t(c.rgbBlue)|(uint32_t(c.rgbGreen)<<8)|(uint32_t(c.rgbRed)<<16)|(uint32_t(c.rgbReserved)<<24));
    }
    if(rleObservations++==rleFailure)throw RleInterrupted();
}
#define VOBSUB_RLE_SHELL(Name) \
class Name { public: \
    CRect rect;bool fCustomPal,fAligned,allocate;int tridx;size_t nOffset[2],nPlane; \
    RGBQUAD *orgpal,*cuspal,*lpPixels,*lpTemp1,*lpTemp2,*backing;size_t backingCount; \
    struct SubPal {BYTE pal:4,tr:4;};SubPal pal[4]; \
    void GetPacketInfo(const BYTE*,size_t,size_t,int){observeRle(*this,1);} \
    bool Alloc(int w,int h){observeRle(*this,2,w,h);return allocate;} \
    void TrimSubImageImpl(); \
    void TrimSubImage(){observeRle(*this,7);TrimSubImageImpl();observeRle(*this,8);} \
    BYTE GetNibbleImpl(const BYTE*); \
    BYTE GetNibble(const BYTE* data){observeRle(*this,3);const BYTE value=GetNibbleImpl(data);observeRle(*this,4,0,0,0,value);return value;} \
    void DrawPixelsImpl(CPoint,int,size_t); \
    void DrawPixels(CPoint p,int length,size_t color){observeRle(*this,5,p.x,p.y,length,color);DrawPixelsImpl(p,length,color);observeRle(*this,6,p.x,p.y,length,color);} \
    bool Decode(BYTE*,size_t,size_t,int,bool,int,RGBQUAD*,RGBQUAD*,bool); \
};
static uint32_t observedNibble(uint8_t byte,uint32_t aligned){++nibbleCalls;return xy_vsfilter_vobsub_nibble(byte,aligned);}
static uint64_t observedCode(uint32_t code,uint8_t nibble,uint32_t stage){++codeCalls;return xy_vsfilter_vobsub_code_step(code,nibble,stage);}
#define xy_vsfilter_vobsub_nibble observedNibble
#define xy_vsfilter_vobsub_code_step observedCode
