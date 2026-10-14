// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_legacy_blur.h"
static uint64_t calls=0,fields=0,observations=0,failures=0,retained=0,layoutExclusions=0;
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
static bool supportedLayout(const Overlay& input,float strength,double gaussian,double sx,double sy){
    int rx=(static_cast<int>(std::ceil(gaussian*sx*3))|1)/2,ry=(static_cast<int>(std::ceil(gaussian*sy*3))|1)/2;
    if(rx<1&&gaussian>ReferenceLegacyBlur::Rasterizer::GAUSSIAN_BLUR_THREHOLD)rx=1;
    if(ry<1&&gaussian>ReferenceLegacyBlur::Rasterizer::GAUSSIAN_BLUR_THREHOLD)ry=1;
    int ax=gaussian>0?rx*8:0,ay=gaussian>0?ry*8:0;
    if(strength){ax+=static_cast<int>(sx*std::sqrt(strength*0.25f)+0.5)*8;ay+=static_cast<int>(sy*std::sqrt(strength*0.25f)+0.5)*8;}
    ax=(ax+7)&~7;ay=(ay+7)&~7;
    int pitch=(input.mOverlayWidth+(ax>>2)+15)&~15,height=input.mOverlayHeight+(ay>>2);
    return xy_vsfilter_plane_copy_fits(input.mOverlayPitch,input.mOverlayHeight,pitch,height,ax>>3,ay>>3)!=0;
}
static void compare(const Overlay& input,float strength,double gaussian,double sx,double sy,int fail){
    if(!supportedLayout(input,strength,gaussian,sx,sy)){++layoutExclusions;return;}
    auto a=std::make_shared<Overlay>(),b=std::make_shared<Overlay>();
    const auto original=input.Snapshot();const size_t initialBytes=g_xy_malloc_used_size;
    for(unsigned round=0;round<2;++round){
        coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;const size_t beforeA=g_xy_malloc_used_size;seed();bool ra=ReferenceLegacyBlur::Rasterizer::OldFixedPointBlur(input,strength,gaussian,sx,sy,a);Effects ea;const auto ta=coverageEvents;const size_t da=g_xy_malloc_used_size-beforeA;
        coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;const size_t beforeB=g_xy_malloc_used_size;seed();bool rb=CandidateLegacyBlur::Rasterizer::OldFixedPointBlur(input,strength,gaussian,sx,sy,b);Effects eb;const auto tb=coverageEvents;const size_t db=g_xy_malloc_used_size-beforeB;
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
static void history(int width,int height,bool alias,float strength,double gaussian,double sx,double sy,int fail){
    Overlay input;input.mWidth=width;input.mHeight=height;input.mOffsetX=-9;input.mOffsetY=13;
    input.mOverlayWidth=((width+7)>>3)+1;input.mOverlayHeight=((height+7)>>3)+1;input.mOverlayPitch=(input.mOverlayWidth+15)&~15;input.mfWideOutlineEmpty=false;
    const size_t count=input.mOverlayPitch*input.mOverlayHeight;allocationFailure=-1;
    input.mBody.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);
    for(size_t i=0;i<count;++i)input.mBody.get()[i]=static_cast<BYTE>(i*71+width*13+height*37);
    if(alias)input.mBorder=input.mBody;
    else {input.mBorder.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);for(size_t i=0;i<count;++i)input.mBorder.get()[i]=static_cast<BYTE>(i*37+width*71+height*13);}
    compare(input,strength,gaussian,sx,sy,fail);input.CleanUp();if(g_xy_malloc_used_size||!coverageAllocations.empty())std::abort();
}
int main(){
    for(int w:{0,1,8,17,111,112,113})for(int h:{0,1,8,17})for(float strength:{0.f,0.25f,1.f,1.5f,3.f})for(double gaussian:{-0.25,0.,0.125,0.333333,0.333334,0.5,1.,2.5}){
        if(!ReferenceLegacyBlur::Rasterizer::IsItReallyBlur(strength,gaussian))continue;
        for(double sx:{0.25,1.,1.25,2.})for(double sy:{0.5,1.,2.})for(bool alias:{false,true}){
            legacyFilterWidth=(w+h)%3==0?1025:3;history(w,h,alias,strength,gaussian,sx,sy,-1);
        }
    }
    for(int failure:{0,1})for(float strength:{0.25f,1.f,3.f})for(bool alias:{false,true})history(113,17,alias,strength,0.5,1.25,0.5,failure);
    Overlay source;
    for(float strength:{0.25f,1.f}){
        seed();bool a=ReferenceLegacyBlur::Rasterizer::OldFixedPointBlur(source,strength,0.5,1.,1.,SharedPtrOverlay());Effects ea;
        seed();bool b=CandidateLegacyBlur::Rasterizer::OldFixedPointBlur(source,strength,0.5,1.,1.,SharedPtrOverlay());Effects eb;
        if(a||b||!(ea==eb))std::abort();++calls;++failures;
    }
    if(!calls||!failures||!retained||!fixedFilterCalls||!fractionalFilterCalls||!legacyFactories||!legacyGaussCalls||!planeOwners||planeOwners!=planeLoans||planeOwners!=planeReleases)std::abort();
    printf("%llu exact legacy-blur calls passed (%llu plane bytes and metadata / %llu logical allocation, factory and filter observations / %llu false returns / %llu retained shared planes / %llu Rust plane owners, loans and releases / %llu coefficient factory callbacks / %llu Gaussian callbacks across both callers / %llu preflight layout exclusions (not parity); complete pinned OldFixedPointBlur and blur predicate, native float extents/pass control, full-pitch source copies including padding, aliased source planes, unchanged input, reuse and modeled plane allocation failures, cache accounting and errno/FP/Windows error state; coefficient factories/filter kernels controlled, both source planes present, object aliasing, invalid geometry, actual OOM and native scratch allocation failures excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)observations,(unsigned long long)failures,(unsigned long long)retained,(unsigned long long)planeOwners,(unsigned long long)legacyFactories,(unsigned long long)legacyGaussCalls,(unsigned long long)layoutExclusions);
}
