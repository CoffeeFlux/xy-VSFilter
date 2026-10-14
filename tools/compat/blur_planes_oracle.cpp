// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_blur_planes.h"
static uint64_t calls=0,fields=0,observations=0,failures=0,retained=0;
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
static void compare(const Overlay& input,float strength,float sx,float sy,int fail){
    auto a=std::make_shared<Overlay>(),b=std::make_shared<Overlay>();
    const auto original=input.Snapshot();const size_t initialBytes=g_xy_malloc_used_size;
    for(unsigned round=0;round<2;++round){
        coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;const size_t beforeA=g_xy_malloc_used_size;seed();bool ra=ReferenceBlurPlanes::Rasterizer::BeBlur(input,strength,sx,sy,a);Effects ea;const auto ta=coverageEvents;const size_t da=g_xy_malloc_used_size-beforeA;
        coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;const size_t beforeB=g_xy_malloc_used_size;seed();bool rb=CandidateBlurPlanes::Rasterizer::BeBlur(input,strength,sx,sy,b);Effects eb;const auto tb=coverageEvents;const size_t db=g_xy_malloc_used_size-beforeB;
        const auto sa=a->Snapshot(),sb=b->Snapshot();
        if(ra!=rb||!(ea==eb)||ta!=tb||da!=db||sa!=sb||original!=input.Snapshot()){
            fprintf(stderr,"Blur plane mismatch call=%llu strength=%g scale=%g,%g fail=%d flags=%d/%d\n",(unsigned long long)calls,strength,sx,sy,fail,ea.flags,eb.flags);std::abort();
        }
        ++calls;failures+=!ra;fields+=sa.size();observations+=ta.size();
    }
    auto aBody=a->mBody,aBorder=a->mBorder,bBody=b->mBody,bBorder=b->mBorder;
    const size_t held=g_xy_malloc_used_size;coverageEvents.clear();a->CleanUp();const auto ca=coverageEvents;coverageEvents.clear();b->CleanUp();
    if(ca!=coverageEvents||g_xy_malloc_used_size!=held)std::abort();
    if(aBody){if(*aBody!=*bBody)std::abort();++retained;}
    if(aBorder){if(*aBorder!=*bBorder)std::abort();++retained;}
    coverageEvents.clear();aBody.reset();aBorder.reset();const auto fa=coverageEvents;coverageEvents.clear();bBody.reset();bBorder.reset();
    if(fa!=coverageEvents||g_xy_malloc_used_size!=initialBytes)std::abort();
}
static void history(int width,int height,bool alias,float strength,float sx,float sy,int fail){
    Overlay input;input.mWidth=width;input.mHeight=height;input.mOffsetX=-9;input.mOffsetY=13;
    input.mOverlayWidth=((width+7)>>3)+1;input.mOverlayHeight=((height+7)>>3)+1;input.mOverlayPitch=(input.mOverlayWidth+15)&~15;input.mfWideOutlineEmpty=false;
    const size_t count=input.mOverlayPitch*input.mOverlayHeight;allocationFailure=-1;
    input.mBody.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);
    for(size_t i=0;i<count;++i)input.mBody.get()[i]=static_cast<BYTE>(i*71+width*13+height*37);
    if(alias)input.mBorder=input.mBody;
    else {input.mBorder.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);for(size_t i=0;i<count;++i)input.mBorder.get()[i]=static_cast<BYTE>(i*37+width*71+height*13);}
    compare(input,strength,sx,sy,fail);input.CleanUp();if(g_xy_malloc_used_size||!coverageAllocations.empty())std::abort();
}
int main(){
    for(int w:{0,1,7,8,9,111,112,113})for(int h:{0,1,7,8,17})for(float strength:{0.125f,0.25f,1.f,1.5f,2.f,3.f,4.f,9.f})for(float sx:{0.25f,0.5f,1.f,1.25f,2.f})for(float sy:{0.25f,1.f,2.f})for(bool alias:{false,true})history(w,h,alias,strength,sx,sy,-1);
    for(int failure:{0,1})for(float strength:{0.125f,1.f,3.f})for(bool alias:{false,true})history(113,17,alias,strength,1.25f,0.5f,failure);
    if(!calls||!failures||!retained||!fixedFilterCalls||!fractionalFilterCalls||!planeOwners||planeOwners!=planeLoans||planeOwners!=planeReleases)std::abort();
    printf("%llu exact whole-BeBlur calls passed (%llu plane bytes and metadata / %llu logical allocation and filter observations / %llu false returns / %llu retained shared planes / %llu Rust plane owners, loans and releases / %llu fixed-filter and %llu fractional-filter callbacks across both callers; native float geometry and pass control, row copies and padding, aliased source planes, unchanged input, reuse and modeled plane allocation failures, cache accounting and errno/FP/Windows error state; filter kernels are controlled, both source planes present, source/output object aliasing, invalid geometry, actual OOM and native scratch allocation failure excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)observations,(unsigned long long)failures,(unsigned long long)retained,(unsigned long long)planeOwners,(unsigned long long)fixedFilterCalls,(unsigned long long)fractionalFilterCalls);
}
