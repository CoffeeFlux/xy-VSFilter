// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_composite_storage.h"
static uint64_t calls=0,fields=0,initialized=0,unwritten=0,nulls=0,exclusions=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
static void effects(std::vector<uint64_t>& out){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
const unsigned long last=GetLastError();
#else
const unsigned long last=0;
#endif
out.insert(out.end(),{static_cast<uint64_t>(error),static_cast<uint64_t>(flags),last});}
static SharedPtrByte input(size_t count,unsigned salt){auto* p=static_cast<BYTE*>(xy_malloc(count));for(size_t i=0;i<count;++i)p[i]=static_cast<BYTE>(i*37+salt);return SharedPtrByte(p,xy_free);}
static void outputSnapshot(const SharedPtrByte& data,std::vector<uint64_t>& out,bool count){
    out.push_back(data?1:0);out.push_back(g_xy_malloc_used_size);if(!data)return;
    compositeRequire(data.get()==compositeDestination);
    for(size_t i=0;i<compositeCount;++i){out.push_back(compositeWritten[i]);if(compositeWritten[i]){out.push_back(data.get()[i]);initialized+=count;}else unwritten+=count;}
}
struct Outcome {std::vector<uint64_t> fields;std::vector<int64_t> events;bool operator==(const Outcome& o)const{return fields==o.fields&&events==o.events;}};
template<class OverlayType,class Raster>static Outcome execute(int w,int h,int sub,int flags,bool border,int maskMode,const CRect& clip,const std::vector<DWORD>& colors,bool decline,bool count){
    compositeRequire(coverageAllocations.empty()&&!g_xy_malloc_used_size);allocationFailure=-1;allocationAttempt=0;compositeDecline=decline;
    auto overlay=std::make_shared<OverlayType>();overlay->mOverlayWidth=w;overlay->mOverlayHeight=h;overlay->mOverlayPitch=(w+15)&~15;overlay->mOffsetX=-11;overlay->mOffsetY=7;
    overlay->mBody=input(overlay->mOverlayPitch*h,19);if(border)overlay->mBorder=input(overlay->mOverlayPitch*h,83);
    GrayImage2 mask;mask.pitch=128;mask.left_top=CPoint(-64,-64);mask.size=CSize(128,128);if(maskMode==1)mask.data=input(128*128,47);
    const auto source=overlay->Snapshot();const size_t sourceBytes=g_xy_malloc_used_size;const auto originalColors=colors;
    coverageEvents.clear();Outcome outcome;auto& out=outcome.fields;
    for(int repeat=0;repeat<2;++repeat){
        compositeDestination=NULL;compositeWritten.clear();compositeCount=0;CRect dirty(17,29,31,37);seed();
        auto result=Raster::CompositeAlphaMask(overlay,clip,maskMode?&mask:NULL,sub,-sub,colors.empty()?NULL:colors.data(),flags&1,flags&2,&dirty);effects(out);
        out.insert(out.end(),{static_cast<uint64_t>(dirty.left),static_cast<uint64_t>(dirty.top),static_cast<uint64_t>(dirty.right),static_cast<uint64_t>(dirty.bottom)});
        nulls+=count&&!result;outputSnapshot(result,out,count);auto alias=result;result.reset();outputSnapshot(alias,out,count);
        compositeRequire(overlay->Snapshot()==source&&colors==originalColors);
        if(mask.data)for(size_t i=0;i<128*128;++i)compositeRequire(mask.data.get()[i]==static_cast<BYTE>(i*37+47));
        seed();alias.reset();effects(out);compositeRequire(g_xy_malloc_used_size==sourceBytes);out.push_back(g_xy_malloc_used_size);
    }
    outcome.events=coverageEvents;overlay.reset();mask.data.reset();compositeRequire(coverageAllocations.empty()&&!g_xy_malloc_used_size);return outcome;
}
static void compare(int w,int h,int sub,int flags,bool border,int maskMode,int clipMode,int colorMode,bool decline){
    const int x=(sub-11+4)>>3,y=(-sub+7+4)>>3;
    CRect clip=clipMode==0?CRect(-64,-64,64,64):clipMode==1?CRect(0,0,5,7):clipMode==2?CRect(80,80,100,100):CRect(0,0,0,0);
    // Multicolor callbacks start at different x offsets. Keep a physical final
    // row for their native post-row cursors, without extending allocations.
    if(colorMode>=2&&clipMode==0)clip.bottom=y+h-1;
    int xo=std::max<int>(0,clip.left-x),yo=std::max<int>(0,clip.top-y);
    int cw=std::min<int>(w-xo,clip.right-std::max<int>(x,clip.left));
    int ch=std::min<int>(h-yo,clip.bottom-std::max<int>(y,clip.top));
    if(colorMode!=0&&flags&&(!(flags&2)||border)&&cw>0&&ch>0&&yo+ch==h&&(xo>0||colorMode>=2)){++exclusions;return;}
    std::vector<DWORD> colors;
    if(colorMode==1)colors={0x7f123456U,0xffffffffU};
    if(colorMode>=2){const DWORD middle=colorMode==2?0xffabcdefU:0x07123456U;colors={0x07123456U,0,middle,static_cast<DWORD>(xo+std::max<int>(0,cw)/2),0x00123456U,static_cast<DWORD>(xo+std::max<int>(0,cw)),0xff000000U,0xffffffffU};}
    const auto a=execute<ReferenceComposite::Overlay,ReferenceComposite::Rasterizer>(w,h,sub,flags,border,maskMode,clip,colors,decline,true);
    const auto b=execute<CandidateComposite::Overlay,CandidateComposite::Rasterizer>(w,h,sub,flags,border,maskMode,clip,colors,decline,false);
    if(!(a==b)){
        fprintf(stderr,"Composite mask mismatch size=%d,%d sub=%d flags=%d border=%d mask=%d clip=%d colors=%d decline=%d\n",w,h,sub,flags,border,maskMode,clipMode,colorMode,decline);
        fprintf(stderr,"Fields reference=%llu candidate=%llu; events reference=%llu candidate=%llu\n",(unsigned long long)a.fields.size(),(unsigned long long)b.fields.size(),(unsigned long long)a.events.size(),(unsigned long long)b.events.size());
        for(size_t i=0;i<std::min(a.fields.size(),b.fields.size());++i)if(a.fields[i]!=b.fields[i])fprintf(stderr,"field[%llu] reference=%llu candidate=%llu\n",(unsigned long long)i,(unsigned long long)a.fields[i],(unsigned long long)b.fields[i]);
        for(size_t i=0;i<std::min(a.events.size(),b.events.size());++i)if(a.events[i]!=b.events[i])fprintf(stderr,"event[%llu] reference=%lld candidate=%lld\n",(unsigned long long)i,(long long)a.events[i],(long long)b.events[i]);
        std::abort();
    }
    calls+=2;fields+=a.fields.size()+a.events.size();
}
int main(){
    for(int w:{1,3,7,16,31})for(int h:{1,3,9})for(int sub:{-17,-8,-1,0,1,7,8,17})for(int flags=0;flags<4;++flags)for(bool border:{false,true})for(int mask=0;mask<3;++mask)for(int clip=0;clip<4;++clip)for(int colors=0;colors<4;++colors)compare(w,h,sub,flags,border,mask,clip,colors,false);
    for(int w:{1,7,31})for(int h:{3,9})for(int colors=0;colors<4;++colors)for(int mask=0;mask<3;++mask)compare(w,h,-1,3,true,mask,0,colors,true);
    if(!calls||!initialized||!unwritten||!nulls||!exclusions||!compositeFills||!planeOwners||planeOwners!=planeLoans||planeLoans!=planeReleases||!compositeDeclines)std::abort();
    printf("%llu exact CompositeAlphaMask calls passed (%llu state/initialization/allocation fields / %llu initialized bytes / %llu unwritten cells tracked without reads / %llu null results / %llu observed complete FillAlphaMash calls / %llu Rust output owners, loans and releases / %llu modeled Rust declines; %llu final-row cursor histories excluded before both implementations and NOT passes; whole pinned caller and fill dispatch, extracted mask kernels, clipping and dirty rectangles, subpixel rounding, body/border/mask selection, bounded single/multiple color switches including equal alpha and empty runs, unchanged inputs and shared-output lifetime, cache accounting and errno/FP/Windows error state; allocation controlled, unbounded switch arrays, invalid backing/geometry, native allocation failure and shared-control-block exceptions excluded; portable builds compare native x64 scalar delegation, Win32 SIMD requires Windows)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initialized,(unsigned long long)unwritten,(unsigned long long)nulls,(unsigned long long)compositeFills,(unsigned long long)planeOwners,(unsigned long long)compositeDeclines,(unsigned long long)exclusions);
}
