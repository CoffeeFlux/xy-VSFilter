// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "span_coverage_shell.h"
#include "xy_rust_mask.h"
struct CPoint {int x,y;CPoint(int a=0,int b=0):x(a),y(b){}void SetPoint(int a,int b){x=a;y=b;}};
struct CSize {int cx,cy;CSize(int a=0,int b=0):cx(a),cy(b){}};
struct GrayImage2 {
    std::shared_ptr<BYTE> data;int pitch;CSize size;CPoint left_top;
};
#define DEBUG_NEW new
static SharedPtrOverlay clipSource;
struct CWordPaintMachine {
    static void PaintBody(void* polygon,CPoint a,CPoint b,SharedPtrOverlay* output){
        if(polygon!=reinterpret_cast<void*>(1)||a.x||a.y||b.x||b.y)std::abort();
        coverageEvents.push_back(8);*output=clipSource;
    }
};
static uint64_t clipRows=0,clipFallbacks=0;
static void clipCheckRange(const void* p,size_t count){
    const uintptr_t address=reinterpret_cast<uintptr_t>(p);
    for(const auto& a:coverageAllocations){const uintptr_t base=reinterpret_cast<uintptr_t>(a.data());
        if(address>=base&&address-base<=a.count&&count<=a.count-(address-base))return;
    }
    fprintf(stderr,"Clip-mask access outside fixture backing\n");std::abort();
}
static void* clipCopy(void* destination,const void* source,size_t count){
    clipCheckRange(destination,count);clipCheckRange(source,count);return std::memcpy(destination,source,count);
}
static BYTE clipRead(const BYTE* p){clipCheckRange(p,1);return *p;}
static void clipWrite(BYTE* p,BYTE value){clipCheckRange(p,1);*p=value;}
static uint32_t observedClipRow(BYTE* dst,const BYTE* src,size_t count,uint32_t inverse){
    clipCheckRange(dst,count);clipCheckRange(src,count);
    uint32_t result=xy_vsfilter_clip_row(dst,src,count,inverse);if(result)++clipRows;else ++clipFallbacks;return result;
}
static void* clipSet(void* p,int value,size_t count){clipCheckRange(p,count);return memsetSSE2(p,value,count);}
#define CLIP_MASK_CLASS class CClipper {public:CSize m_size;bool m_inverse;void* m_polygon;CClipper(int w,int h,bool inverse):m_size(w,h),m_inverse(inverse),m_polygon(reinterpret_cast<void*>(1)){}GrayImage2* PaintBaseClipper();};
