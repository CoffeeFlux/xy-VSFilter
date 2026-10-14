// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "span_coverage_shell.h"
#include "original_mask.h"
#ifdef _WIN32
#include <atltypes.h>
#else
typedef uint32_t DWORD;
struct CPoint {int x,y;CPoint(int a=0,int b=0):x(a),y(b){}};
struct CSize {int cx,cy;CSize(int a=0,int b=0):cx(a),cy(b){}};
struct CRect {int left,top,right,bottom;CRect(int l=0,int t=0,int r=0,int b=0):left(l),top(t),right(r),bottom(b){}CRect(CPoint p,CSize s):left(p.x),top(p.y),right(p.x+s.cx),bottom(p.y+s.cy){}void SetRect(int l,int t,int r,int b){left=l;top=t;right=r;bottom=b;}CRect& operator&=(const CRect& o){left=std::max(left,o.left);top=std::max(top,o.top);right=std::min(right,o.right);bottom=std::min(bottom,o.bottom);if(left>=right||top>=bottom)SetRect(0,0,0,0);return *this;}};
#endif
typedef BYTE byte;
typedef std::shared_ptr<BYTE> SharedPtrByte;
struct GrayImage2 {SharedPtrByte data;int pitch;CSize size;CPoint left_top;};
static bool compositeDecline=false;
static uint64_t compositeDeclines=0,compositeFills=0;
static std::vector<bool> compositeWritten;
static BYTE* compositeDestination=NULL;
static size_t compositeCount=0;
static void compositeRequire(bool value){if(!value){fprintf(stderr,"Composite-mask observation outside scope\n");std::abort();}}
static size_t compositeExtent(const BYTE* data,size_t count){
    uintptr_t p=reinterpret_cast<uintptr_t>(data);
    for(const auto& region:coverageAllocations){uintptr_t base=reinterpret_cast<uintptr_t>(region.data());if(p>=base&&p-base<=region.count&&count<=region.count-(p-base))return p-base;}
    compositeRequire(false);return 0;
}
namespace XyRust {
class ObservedCompositeAllocation:public PlaneAllocation {public:explicit ObservedCompositeAllocation(size_t count):PlaneAllocation(compositeDecline?0:count){compositeDeclines+=compositeDecline;}};
}
static void beforeCompositeFill(const ::Overlay& overlay,BYTE* dst,bool body,bool border,int x,int y,int w,int h,const BYTE* mask,int maskPitch,DWORD alpha){
    compositeRequire(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=overlay.mOverlayWidth&&y+h<=overlay.mOverlayHeight);
    const size_t count=static_cast<size_t>(overlay.mOverlayPitch)*overlay.mOverlayHeight;
    compositeExtent(dst,count);
    if(!compositeDestination){compositeDestination=dst;compositeCount=count;compositeWritten.assign(count,false);}else compositeRequire(compositeDestination==dst&&compositeCount==count);
    coverageEvents.insert(coverageEvents.end(),{7,body,border,x,y,w,h,mask?1:0,maskPitch,alpha,static_cast<int64_t>(g_xy_malloc_used_size)});
    if(mask)coverageEvents.push_back(compositeExtent(mask,0));
    for(int row=0;row<h;++row){
        if(body&&!border)compositeExtent(overlay.mBody.get()+(y+row)*overlay.mOverlayPitch+x,w);
        if(border)compositeExtent(overlay.mBorder.get()+(y+row)*overlay.mOverlayPitch+x,w);
        if(border&&!body)compositeExtent(overlay.mBody.get()+(y+row)*overlay.mOverlayPitch+x,w);
        if(mask)compositeExtent(mask+row*maskPitch,w);
    }
}
static void afterCompositeFill(const ::Overlay& overlay,BYTE* dst,int x,int y,int w,int h){
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)compositeWritten[static_cast<size_t>(y+row)*overlay.mOverlayPitch+x+col]=true;
    for(const auto& region:coverageAllocations)checkCoverageGuards(region);
    (void)dst;++compositeFills;
}
#define COMPOSITE_CLASSES \
struct Overlay:public ::Overlay { \
    void FillAlphaMashImpl(byte*,bool,bool,int,int,int,int,const byte*,int,DWORD); \
    void FillAlphaMash(byte* dst,bool body,bool border,int x,int y,int w,int h,const byte* mask,int pitch,DWORD alpha){ \
        beforeCompositeFill(*this,dst,body,border,x,y,w,h,mask,pitch,alpha); \
        FillAlphaMashImpl(dst,body,border,x,y,w,h,mask,pitch,alpha); \
        afterCompositeFill(*this,dst,x,y,w,h); \
    } \
}; \
typedef std::shared_ptr<Overlay> SharedPtrOverlay; \
class Rasterizer {public:static SharedPtrByte CompositeAlphaMask(const SharedPtrOverlay&,const CRect&,const GrayImage2*,int,int,const DWORD*,bool,bool,CRect*);};
#define ASSERT(x) do{if(!(x))std::abort();}while(0)
#define XY_LOG_FATAL(x) ((void)0)
