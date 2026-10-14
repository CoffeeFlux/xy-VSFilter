// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_variance.h"
static uint64_t calls=0,fields=0,observations=0,wholeCopies=0,rowCopies=0,retained=0,filterFailures=0;
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
static void compare(const ::Overlay& input,unsigned x,unsigned y,int fail){
    ReferenceVariance::Overlay a;CandidateVariance::Overlay b;
    static_cast<::Overlay&>(a)=input;static_cast<::Overlay&>(b)=input;
    const auto original=input.Snapshot();const size_t initialBytes=g_xy_malloc_used_size;
    coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;seed();auto* ra=a.GetSubpixelVariance(x,y);Effects ea;const auto ta=coverageEvents;const size_t deltaA=g_xy_malloc_used_size-initialBytes;
    const size_t beforeB=g_xy_malloc_used_size;
    coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;seed();auto* rb=b.GetSubpixelVariance(x,y);Effects eb;const auto tb=coverageEvents;const size_t deltaB=g_xy_malloc_used_size-beforeB;
    if(!ra||!rb)std::abort();// Plane/object allocation failures are outside this reference campaign.
    const auto sa=ra->Snapshot(),sb=rb->Snapshot();
    if(!(ea==eb)||ta!=tb||deltaA!=deltaB||sa!=sb||original!=input.Snapshot()){
        fprintf(stderr,"Variance mismatch call=%llu shift=%u,%u failure=%d\n",(unsigned long long)calls,x,y,fail);std::abort();
    }
    ++calls;fields+=sa.size();observations+=ta.size();filterFailures+=fail>=0;
    if(ra->mOverlayPitch==input.mOverlayPitch&&ra->mOverlayWidth==input.mOverlayWidth)++wholeCopies;else ++rowCopies;
    auto aBody=ra->mBody,aBorder=ra->mBorder,bBody=rb->mBody,bBorder=rb->mBorder;
    const size_t held=g_xy_malloc_used_size;
    coverageEvents.clear();delete ra;const auto da=coverageEvents;coverageEvents.clear();delete rb;
    if(da!=coverageEvents||g_xy_malloc_used_size!=held||*aBody!=*bBody)std::abort();++retained;
    if(aBorder){if(*aBorder!=*bBorder)std::abort();++retained;}
    coverageEvents.clear();aBody.reset();aBorder.reset();const auto fa=coverageEvents;coverageEvents.clear();bBody.reset();bBorder.reset();
    if(fa!=coverageEvents||g_xy_malloc_used_size!=initialBytes)std::abort();
}
static void history(int width,int height,bool border,bool alias,unsigned x,unsigned y,int fail){
    ::Overlay input;input.mWidth=width;input.mHeight=height;input.mOffsetX=-13;input.mOffsetY=17;
    input.mOverlayWidth=((width+7)>>3)+1;input.mOverlayHeight=((height+7)>>3)+1;input.mOverlayPitch=(input.mOverlayWidth+15)&~15;input.mfWideOutlineEmpty=!border;
    const size_t count=input.mOverlayPitch*input.mOverlayHeight;
    allocationFailure=-1;input.mBody.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);
    for(size_t i=0;i<count;++i)input.mBody.get()[i]=static_cast<BYTE>(i*67+width*13+height*31);
    if(border){
        if(alias)input.mBorder=input.mBody;
        else {input.mBorder.reset(static_cast<BYTE*>(xy_malloc(count)),xy_free);for(size_t i=0;i<count;++i)input.mBorder.get()[i]=static_cast<BYTE>(i*31+width*67+height*13);}
    }
    compare(input,x,y,fail);compare(input,x,y,fail);
    input.CleanUp();if(g_xy_malloc_used_size||!coverageAllocations.empty())std::abort();
}
int main(){
    for(int w:{0,1,7,8,9,15,16,17,111,112,113,119,120,121})for(int h:{0,1,7,8,9,31})for(unsigned x=0;x<8;++x)for(unsigned y=0;y<8;++y)for(unsigned mode=0;mode<3;++mode)history(w,h,mode!=0,mode==2,x,y,-1);
    for(unsigned x:{0u,7u,8u,15u,UINT32_MAX})for(unsigned y:{0u,7u,8u,UINT32_MAX})for(int mode=0;mode<3;++mode){
        const int firstTemporary=mode?2:1;
        history(113,17,mode!=0,mode==2,x,y,firstTemporary);
        if(mode)history(113,17,true,mode==2,x,y,firstTemporary+1);
    }
    if(!calls||!wholeCopies||!rowCopies||!filterFailures||!planeOwners||planeOwners!=planeLoans||planeOwners!=planeReleases||!varianceRows)std::abort();
    printf("%llu exact subpixel-overlay calls passed (%llu plane bytes and metadata / %llu logical allocation observations / %llu whole-pitch copies / %llu row-width copies / %llu modeled filter scratch failures / %llu retained shared planes / %llu Rust plane owners, loans and releases / %llu Rust bilinear rows; complete pinned GetSubpixelVariance and Bilinear, all subpixel phases, packed-pitch boundaries, source padding and aliases, unchanged input, cache byte accounting and errno/FP/Windows error state; object/plane allocation failures, inconsistent source metadata, out-of-backing copies and arithmetic overflow excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)observations,(unsigned long long)wholeCopies,(unsigned long long)rowCopies,(unsigned long long)filterFailures,(unsigned long long)retained,(unsigned long long)planeOwners,(unsigned long long)varianceRows);
}
