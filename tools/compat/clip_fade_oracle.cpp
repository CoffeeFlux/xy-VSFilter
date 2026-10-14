// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_clip_fade.h"
static uint64_t calls=0,fields=0,observations=0,changes=0,nulls=0,zeroExclusions=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
struct Effects {int error,flags;unsigned long last;Effects():error(errno),flags(std::fetestexcept(FE_ALL_EXCEPT)),last(0){
#ifdef _WIN32
last=GetLastError();
#endif
}bool operator==(const Effects& o)const{return error==o.error&&flags==o.flags&&last==o.last;}};
static std::vector<BYTE> snapshot(GrayImage2* output){
    if(!output)return {};const size_t count=static_cast<size_t>(output->size.cx)*output->size.cy;clipCheckRange(output->data.get(),count);
    for(const auto& allocation:coverageAllocations)checkCoverageGuards(allocation);
    return std::vector<BYTE>(output->data.get(),output->data.get()+count);
}
static void compare(int w,int h,int fade,double scale,int top,int bottom,unsigned salt,bool null,bool scroll){
    if(!null&&static_cast<int>(fade*scale)==0){++zeroExclusions;return;}
    fadeNull=null;fadeInput.resize(static_cast<size_t>(w)*h);for(size_t i=0;i<fadeInput.size();++i)fadeInput[i]=static_cast<BYTE>(i*37+salt);const auto source=fadeInput;
    ReferenceFade::CClipper a(w,h,fade,scale,top,bottom);CandidateFade::CClipper b(w,h,fade,scale,top,bottom);
    for(int round=0;round<2;++round){
        coverageEvents.clear();seed();auto* ra=scroll?a.PaintScrollClipper():a.PaintBannerClipper();Effects ea;const auto trace=coverageEvents;const auto sa=snapshot(ra);
        coverageEvents.clear();seed();auto* rb=scroll?b.PaintScrollClipper():b.PaintBannerClipper();Effects eb;const auto sb=snapshot(rb);
        if(sa!=sb||!(ea==eb)||trace!=coverageEvents||fadeInput!=source){fprintf(stderr,"Clip fade mismatch scroll=%d size=%d,%d fade=%d scale=%g top=%d bottom=%d flags=%d/%d\n",scroll,w,h,fade,scale,top,bottom,ea.flags,eb.flags);std::abort();}
        ++calls;fields+=sa.size();observations+=trace.size();nulls+=ra==NULL;changes+=!null&&sa!=source;
        if(ra&&(!rb||ra->pitch!=rb->pitch||ra->size.cx!=rb->size.cx||ra->size.cy!=rb->size.cy||ra->left_top.x!=rb->left_top.x||ra->left_top.y!=rb->left_top.y))std::abort();
        coverageEvents.clear();delete ra;const auto freed=coverageEvents;coverageEvents.clear();delete rb;
        if(freed!=coverageEvents||g_xy_malloc_used_size||!coverageAllocations.empty())std::abort();
    }
}
int main(){
    for(int w:{1,2,3,7,16,33})for(int h:{1,3,7,17})for(int fade:{-3,-1,0,1,2,3,7,16,33,127,16385})for(double scale:{0.25,0.5,1.,1.25,2.}){
        for(bool null:{false,true})compare(w,h,fade,scale,0,0,19,null,false);
        for(int top:{-129,-8,0,7,8,31,129})for(int bottom:{-65,0,8,63,257})compare(w,h,fade,scale,top,bottom,37,false,true);
        compare(w,h,fade,scale,31,-8,255,true,true);
    }
    for(unsigned value=0;value<256;++value)for(int fade:{1,3,7}){compare(3,4,fade,1.,-8,16,value,false,false);compare(3,4,fade,1.,-8,16,value,false,true);}
    if(!calls||!changes||!nulls||!zeroExclusions||!bannerCalls||!bannerDeclines||!scrollCalls||scrollDeclines)std::abort();
    printf("%llu exact whole banner/scroll clipper calls passed (%llu output bytes / %llu allocation-paint observations / %llu changed outputs / %llu null results / %llu Rust banner passes / %llu retained negative-width banner fallbacks / %llu Rust scroll passes / 0 scroll fallbacks; %llu zero-divisor histories excluded before either call and NOT parity passes; whole pinned native float conversions and their order, integer divisions, overlapping fades, signed offscreen/reversed endpoints, retained empty-interval clearing, byte narrowing, all source byte values, reuse and errno/FP/Windows error state; initialized base-mask producer controlled, native overflowing geometry/arithmetic, invalid backing and actual OOM excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)observations,(unsigned long long)changes,(unsigned long long)nulls,(unsigned long long)bannerCalls,(unsigned long long)bannerDeclines,(unsigned long long)scrollCalls,(unsigned long long)zeroExclusions);
}
