// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_clip_mask.h"
static uint64_t calls=0,fields=0,nulls=0,retained=0,observations=0,excluded=0;
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
static std::vector<uint64_t> snapshot(const GrayImage2* image){
    if(!image)return {};
    std::vector<uint64_t> result={static_cast<uint64_t>(image->pitch),static_cast<uint64_t>(image->size.cx),static_cast<uint64_t>(image->size.cy),static_cast<uint64_t>(image->left_top.x),static_cast<uint64_t>(image->left_top.y)};
    const size_t count=static_cast<size_t>(image->size.cx)*image->size.cy;clipCheckRange(image->data.get(),count);
    result.insert(result.end(),image->data.get(),image->data.get()+count);return result;
}
static bool supported(int width,int height,const Overlay& source){
    if(width<=0||height<=0)return true;
    int64_t x=(source.mOffsetX+4)>>3,y=(source.mOffsetY+4)>>3;
    const int64_t sx=std::max<int64_t>(-x,0),sy=std::max<int64_t>(-y,0);
    int64_t w=source.mOverlayWidth-sx,h=source.mOverlayHeight-sy;x=std::max<int64_t>(x,0);y=std::max<int64_t>(y,0);
    w=std::min<int64_t>(w,width-x);h=std::min<int64_t>(h,height-y);
    if(w<=0||h<=0)return true;
    // The reference advances both row pointers after its final copy. Exclude
    // histories whose final cursor would exceed the actual allocation, before
    // invoking either implementation. Exclusions are never parity passes.
    return sx+w<=source.mOverlayPitch&&sy+h<=source.mOverlayHeight&&
        source.mOverlayPitch*(sy+h)+sx<=int64_t(source.mOverlayPitch)*source.mOverlayHeight&&
        int64_t(width)*(y+h)+x<=int64_t(width)*height;
}
static void compare(int width,int height,bool inverse,int failure){
    if(!supported(width,height,*clipSource)){++excluded;return;}
    ReferenceClip::CClipper a(width,height,inverse);CandidateClip::CClipper b(width,height,inverse);
    const auto source=clipSource->Snapshot();const size_t initial=g_xy_malloc_used_size;
    for(int reuse=0;reuse<2;++reuse){
        coverageEvents.clear();allocationAttempt=0;allocationFailure=failure;seed();auto* ra=a.PaintBaseClipper();Effects ea;const auto trace=coverageEvents;const size_t delta=g_xy_malloc_used_size-initial;const auto sa=snapshot(ra);
        coverageEvents.clear();allocationAttempt=0;allocationFailure=failure;const size_t before=g_xy_malloc_used_size;seed();auto* rb=b.PaintBaseClipper();Effects eb;const auto sb=snapshot(rb);
        if(sa!=sb||!(ea==eb)||trace!=coverageEvents||delta!=g_xy_malloc_used_size-before||source!=clipSource->Snapshot()){
            fprintf(stderr,"Clip mask mismatch call %llu size %d,%d inverse %d failure %d\n",(unsigned long long)calls,width,height,inverse,failure);std::abort();
        }
        ++calls;fields+=sa.size();nulls+=ra==NULL;observations+=trace.size();
        std::shared_ptr<BYTE> holdA,holdB;if(ra){holdA=ra->data;holdB=rb->data;}
        const size_t held=g_xy_malloc_used_size;coverageEvents.clear();delete ra;const auto deleted=coverageEvents;coverageEvents.clear();delete rb;
        if(deleted!=coverageEvents||g_xy_malloc_used_size!=held)std::abort();
        if(holdA){if(holdA.get()[0]!=holdB.get()[0])std::abort();++retained;}
        coverageEvents.clear();holdA.reset();const auto released=coverageEvents;coverageEvents.clear();holdB.reset();
        if(released!=coverageEvents||g_xy_malloc_used_size!=initial)std::abort();
    }
}
static void history(int width,int height,int sourceWidth,int sourceHeight,int x,int y,unsigned salt){
    clipSource=std::make_shared<Overlay>();auto& input=*clipSource;input.mOverlayWidth=sourceWidth;input.mOverlayHeight=sourceHeight;input.mOverlayPitch=(sourceWidth+15)&~15;
    input.mOffsetX=x;input.mOffsetY=y;const size_t count=static_cast<size_t>(input.mOverlayPitch)*sourceHeight;
    allocationFailure=-1;input.mBody.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);
    for(size_t i=0;i<count;++i)input.mBody.get()[i]=static_cast<BYTE>(i*37+salt);
    for(bool inverse:{false,true})for(int failure:{-1,0})compare(width,height,inverse,failure);
    clipSource.reset();if(!coverageAllocations.empty()||g_xy_malloc_used_size)std::abort();
}
int main(){
    for(int w:{-1,0,1,3,16,31,64})for(int h:{-1,0,1,7,32})for(int sw:{0,1,7,16,33})for(int sh:{0,1,3,17})for(int x:{-67,-12,-4,0,3,4,9,128})for(int y:{-25,-4,0,4,43})history(w,h,sw,sh,x,y,19);
    for(unsigned byte=0;byte<256;++byte)history(8,4,1,1,0,0,byte);
    uint32_t random=0x173ea49b;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<2000;++i){const int w=1+next()%129,h=1+next()%65,sw=next()%129,sh=next()%65,x=static_cast<int>(next()%1025)-512,y=static_cast<int>(next()%513)-256;history(w,h,sw,sh,x,y,next());}
    if(!calls||!retained||!excluded||!planeOwners||planeOwners!=planeLoans||planeLoans!=planeReleases||!clipRows||clipFallbacks)std::abort();
    printf("%llu exact whole-PaintBaseClipper calls passed (%llu output bytes and metadata / %llu logical allocation-paint observations / %llu null returns / %llu retained masks / %llu Rust owners, loans and releases / %llu Rust active-row copies / 0 row fallbacks; %llu final-cursor footprint exclusions are NOT parity passes; whole pinned geometry, normal/inverse byte narrowing, all source byte values, offscreen/empty/reuse, modeled mask-allocation failure, shared lifetime, cache accounting and errno/FP/Windows error state; polygon producer controlled, native overflowing geometry, object OOM and invalid backings excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)observations,(unsigned long long)nulls,(unsigned long long)retained,(unsigned long long)planeOwners,(unsigned long long)clipRows,(unsigned long long)excluded);
}
