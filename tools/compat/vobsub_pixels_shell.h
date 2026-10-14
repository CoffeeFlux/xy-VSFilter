// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include "xy_rust_vobsub.h"
typedef uint8_t BYTE;
struct CPoint { int32_t x,y; CPoint(int32_t a=0,int32_t b=0):x(a),y(b){} };
struct CRect {
    int32_t left,top,right,bottom;
    CRect(int32_t l=0,int32_t t=0,int32_t r=0,int32_t b=0):left(l),top(t),right(r),bottom(b){}
    int32_t Width() const {return right-left;}
};
struct RGBQUAD { BYTE rgbBlue,rgbGreen,rgbRed,rgbReserved; };
static uint64_t pixelHandled=0,pixelFallback=0,pixelFilled=0;
static uint64_t observedClip(int32_t l,int32_t t,int32_t r,int32_t b,int32_t x,int32_t y,int32_t length) {
    const uint64_t result=xy_vsfilter_vobsub_clip(l,t,r,b,x,y,length);
    pixelHandled+=result!=static_cast<uint64_t>(-1);pixelFallback+=result==static_cast<uint64_t>(-1);return result;
}
static uint32_t observedFill(uint8_t* destination,size_t pixels,uint32_t color) {
    const uint32_t result=xy_vsfilter_vobsub_fill(destination,pixels,color);pixelFilled+=result!=0;return result;
}
#define xy_vsfilter_vobsub_clip observedClip
#define xy_vsfilter_vobsub_fill observedFill
#define VOBSUB_PIXELS_SHELL(Name) \
class Name { public: \
    CRect rect;bool fCustomPal;RGBQUAD *orgpal,*cuspal,*lpPixels; \
    struct SubPal {BYTE pal:4,tr:4;};SubPal pal[4]; \
    void DrawPixels(CPoint,int,size_t); \
};
