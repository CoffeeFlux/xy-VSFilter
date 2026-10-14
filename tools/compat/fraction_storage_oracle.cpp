// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_fraction_storage.h"
static uint64_t calls=0,bytes=0,historyFields=0,events=0,changed=0;
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
static void compare(int w,int h,int padding,float x,float y,bool decline,unsigned salt){
    const int pitch=w+padding;
    std::vector<uint8_t> a(pitch*h+32,0xa5);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)a[16+row*pitch+col]=static_cast<uint8_t>(row*37+col*71+salt);
    auto b=a;const auto initial=a;fractionInputCount=pitch*h;
    for(int repeat=0;repeat<2;++repeat){
        fractionDecline=decline;fractionInput=a.data()+16;fractionEvents.clear();fractionHistory.clear();seed();
        ReferenceFraction::xy_be_blur(fractionInput,w,h,pitch,x,y);Effects ea;const auto trace=fractionEvents,history=fractionHistory;
        if(fractionData||fractionOwner||g_xy_malloc_used_size)std::abort();
        fractionInput=b.data()+16;fractionEvents.clear();fractionHistory.clear();seed();
        CandidateFraction::xy_be_blur(fractionInput,w,h,pitch,x,y);Effects eb;
        if(a!=b||trace!=fractionEvents||history!=fractionHistory||!(ea==eb)||fractionData||fractionOwner||g_xy_malloc_used_size){fprintf(stderr,"Fractional blur mismatch %d,%d padding=%d passes=%g,%g decline=%d flags=%d/%d\n",w,h,padding,x,y,decline,ea.flags,eb.flags);std::abort();}
        for(size_t i=0;i<a.size();++i)if(i<16||i>=a.size()-16||((i-16)%pitch)>=static_cast<size_t>(w))if(a[i]!=initial[i])std::abort();
        ++calls;bytes+=a.size();historyFields+=history.size();events+=trace.size();changed+=a!=initial;
    }
}
int main(){
    for(int w:{4,7,8,9,16,17,31})for(int h:{4,7,8,9,17})for(int padding:{0,1,16})
        for(float x:{-3.f,-0.5f,0.f,0.125f,0.5f,0.875f,1.f,1.25f,1.5f,2.75f,7.f})
        for(float y:{-1.f,-0.25f,0.f,0.25f,0.75f,1.f,1.5f,2.125f,3.f,7.5f})
        for(bool decline:{false,true})compare(w,h,padding,x,y,decline,19);
    for(unsigned salt=0;salt<256;++salt)compare(9,7,3,0.5f,1.25f,false,salt);
    if(!calls||!changed||!fractionOwners||fractionOwners!=fractionLoans||fractionOwners!=fractionReleases||!fractionDeclines||!fractionStages)std::abort();
    printf("%llu exact fractional-blur calls passed (%llu output/guard bytes / %llu initialized stage/scratch fields / %llu allocation-stage observations / %llu changed outputs / %llu Rust scratch owners, loans and releases / %llu modeled Rust declines to native allocation; whole pinned xy_be_blur and xy_calculate_filter, two transposes, exact filter ordering, padding and cache accounting, repeated calls, signed/integer/fractional passes and strict-FP errno/flags/Windows error state; %s; dimensions at least four, native before-base cursor domains, invalid backing, overflowing/nonfinite passes and actual allocation failure excluded)\n",(unsigned long long)calls,(unsigned long long)bytes,(unsigned long long)historyFields,(unsigned long long)events,(unsigned long long)changed,(unsigned long long)fractionOwners,(unsigned long long)fractionDeclines,FILTER_NATIVE_SSE?"actual extracted native SSE versus candidate Rust kernels":"extracted scalar filter callbacks on arm64, NOT SSE parity");
}
