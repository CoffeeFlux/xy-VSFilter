// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_gauss_planes.h"
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
static void compare(const Overlay& input,double strength,double sx,double sy,int fail){
    auto a=std::make_shared<Overlay>(),b=std::make_shared<Overlay>();
    const auto original=gaussPlaneSnapshot(input);const size_t initialBytes=g_xy_malloc_used_size;
    for(unsigned round=0;round<2;++round){
        coverageEvents.clear();allocationAttempt=factoryAxis=0;allocationFailure=fail;const size_t beforeA=g_xy_malloc_used_size;seed();bool ra=ReferenceGaussPlanes::Rasterizer::GaussianBlur(input,strength,sx,sy,a);Effects ea;const auto ta=coverageEvents;const size_t da=g_xy_malloc_used_size-beforeA;
        coverageEvents.clear();allocationAttempt=factoryAxis=0;allocationFailure=fail;const size_t beforeB=g_xy_malloc_used_size;seed();bool rb=CandidateGaussPlanes::Rasterizer::GaussianBlur(input,strength,sx,sy,b);Effects eb;const auto tb=coverageEvents;const size_t db=g_xy_malloc_used_size-beforeB;
        const auto sa=gaussPlaneSnapshot(*a,true),sb=gaussPlaneSnapshot(*b);
        if(ra!=rb||!(ea==eb)||ta!=tb||da!=db||sa!=sb||original!=gaussPlaneSnapshot(input)){
            fprintf(stderr,"Gaussian planes mismatch call=%llu strength=%g scale=%g,%g fail=%d flags=%d/%d\n",(unsigned long long)calls,strength,sx,sy,fail,ea.flags,eb.flags);std::abort();
        }
        ++calls;failures+=!ra;fields+=sa.size();observations+=ta.size();
    }
    auto aBody=a->mBody,aBorder=a->mBorder,bBody=b->mBody,bBorder=b->mBorder;
    const size_t held=g_xy_malloc_used_size;coverageEvents.clear();a->CleanUp();const auto ca=coverageEvents;coverageEvents.clear();b->CleanUp();
    if(ca!=coverageEvents||g_xy_malloc_used_size!=held)std::abort();
    if(aBody){if(readInitialized(aBody.get())!=readInitialized(bBody.get()))std::abort();++retained;}
    if(aBorder){if(readInitialized(aBorder.get())!=readInitialized(bBorder.get()))std::abort();++retained;}
    coverageEvents.clear();aBody.reset();aBorder.reset();const auto fa=coverageEvents;coverageEvents.clear();bBody.reset();bBorder.reset();
    if(fa!=coverageEvents||g_xy_malloc_used_size!=initialBytes)std::abort();
}
static void history(int width,int height,int mode,double strength,double sx,double sy,int rx,int ry,int fail){
    Overlay input;input.mWidth=width;input.mHeight=height;input.mOffsetX=-9;input.mOffsetY=13;
    input.mOverlayWidth=((width+7)>>3)+1;input.mOverlayHeight=((height+7)>>3)+1;input.mOverlayPitch=(input.mOverlayWidth+15)&~15;input.mfWideOutlineEmpty=mode==0||mode==3;
    const size_t count=input.mOverlayPitch*input.mOverlayHeight;allocationFailure=-1;factoryRadii[0]=rx;factoryRadii[1]=ry;
    input.mBody.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);registerInitialization(input.mBody.get(),count,true);
    for(size_t i=0;i<count;++i)input.mBody.get()[i]=static_cast<BYTE>(i*71+width*13+height*37);
    if(mode){if(mode==2)input.mBorder=input.mBody;else {input.mBorder.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);registerInitialization(input.mBorder.get(),count,true);for(size_t i=0;i<count;++i)input.mBorder.get()[i]=static_cast<BYTE>(i*37+width*71+height*13);}}
    compare(input,strength,sx,sy,fail);input.CleanUp();if(g_xy_malloc_used_size||!coverageAllocations.empty())std::abort();planeInitialization.clear();
}
int main(){
    for(int w:{0,1,7,8,9,111,112,113})for(int h:{0,1,7,8,17})for(int mode=0;mode<4;++mode)for(int rx:{0,1,3,7})for(int ry:{0,1,3,7})history(w,h,mode,0.5,1.25,0.5,rx,ry,-1);
    for(double strength:{0.125,0.333333,0.333334,1.,2.5})for(double sx:{0.25,1.,2.})for(double sy:{0.25,1.,2.})for(int mode=0;mode<4;++mode)history(113,17,mode,strength,sx,sy,3,1,-1);
    for(int mode:{1,2})for(int rx:{0,1,3,7})for(int ry:{0,1,3,7})history(113,17,mode,0.5,1.25,0.5,rx,ry,1);
    if(!calls||!failures||!retained||!unwrittenComparisons||!gaussPlaneFactories||!gaussPlaneKernels||!planeOwners||planeOwners!=planeLoans||planeOwners!=planeReleases)std::abort();
    printf("%llu exact whole-GaussianBlur calls passed (%llu metadata and initialized-state fields / %llu initialized plane bytes / %llu unwritten cells tracked without reads / %llu logical allocation, factory and filter observations / %llu false returns / %llu retained shared planes / %llu Rust plane owners, loans and releases; complete pinned caller, controlled coefficients/kernel, body/border selection and ignored extra input border, source aliases, native zero/copy footprints and untouched padding, repeated output reuse, modeled second-plane failure, cache accounting and errno/FP/Windows error state; first-plane failure, object aliasing, inconsistent geometry and actual OOM excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initializedComparisons,(unsigned long long)unwrittenComparisons,(unsigned long long)observations,(unsigned long long)failures,(unsigned long long)retained,(unsigned long long)planeOwners);
}
