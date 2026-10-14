// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "xy_rust_vobsub.h"
#ifdef _WIN32
#define NOMINMAX
#include <atltypes.h>
#else
typedef uint32_t DWORD;
struct RGBQUAD { uint8_t rgbBlue,rgbGreen,rgbRed,rgbReserved; };
struct CPoint { int32_t x,y; CPoint(int32_t a=0,int32_t b=0):x(a),y(b){} };
struct CSize { int32_t cx,cy; CSize(int32_t x,int32_t y):cx(x),cy(y){} };
struct CRect {
    int32_t left,top,right,bottom;
    CRect(int32_t l=0,int32_t t=0,int32_t r=0,int32_t b=0):left(l),top(t),right(r),bottom(b){}
    CRect(CPoint p,CSize s):left(p.x),top(p.y),right(p.x+s.cx),bottom(p.y+s.cy){}
    int32_t Width() const {return right-left;}
    int32_t Height() const {return bottom-top;}
    CPoint TopLeft() const {return CPoint(left,top);}
    CSize Size() const {return CSize(Width(),Height());}
    void operator+=(const CRect& r){left-=r.left;top-=r.top;right+=r.right;bottom+=r.bottom;}
    void operator&=(const CRect& r){
        left=std::max(left,r.left);top=std::max(top,r.top);right=std::min(right,r.right);bottom=std::min(bottom,r.bottom);
        if(left>=right || top>=bottom)left=top=right=bottom=0;
    }
    CRect operator+(CPoint p)const{return CRect(left+p.x,top+p.y,right+p.x,bottom+p.y);}
};
#define ZeroMemory(p,n) std::memset(p,0,n)
#endif
static uint64_t boundsHandled=0,boundsFallback=0,cropsHandled=0,cropsFallback=0;
static uint32_t observedBounds(const uint8_t* source,int32_t width,int32_t height,int32_t* output){
    const auto result=xy_vsfilter_vobsub_bounds(source,width,height,output);boundsHandled+=result!=0;boundsFallback+=result==0;return result;
}
static uint32_t observedCrop(const uint8_t* source,int32_t pitch,int32_t width,int32_t height,uint8_t* destination,const uint8_t* object,size_t size){
    const auto result=xy_vsfilter_vobsub_crop(source,pitch,width,height,destination,object,size);cropsHandled+=result!=0;cropsFallback+=result==0;return result;
}
#define xy_vsfilter_vobsub_bounds observedBounds
#define xy_vsfilter_vobsub_crop observedCrop
#define VOBSUB_TRIM_SHELL(Name) \
class Name { public: \
    CRect rect;RGBQUAD *lpTemp1,*lpTemp2,*lpPixels;RGBQUAD self[256]; \
    void TrimSubImage(); \
};
