// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_gauss_storage.h"
static uint64_t calls=0,outputBytes=0,scratchFields=0,observations=0,declines=0;
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
static void compare(int w,int h,int rx,int ry,int padding,bool decline){
    int ss=((w+15)&~15)+padding,ds=((w+2*rx+15)&~15)+padding;
    std::vector<uint8_t> source(ss*h+96),a(ds*(h+2*ry)+96,0xa5),b=a;
    for(size_t i=0;i<source.size();++i)source[i]=static_cast<uint8_t>(i*71+w*13+h*37);
    const auto original=source;std::vector<float> x(32,0.25f),y(32,0.5f);
    auto aligned=[](std::vector<uint8_t>& storage){return reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(storage.data()+32)+15)&~uintptr_t(15));};
    uint8_t* aData=aligned(a);uint8_t* bData=aligned(b);
    gaussInput=aligned(source);gaussInputCount=ss*h;gaussOutputCount=ds*(h+2*ry);
    for(unsigned repeat=0;repeat<2;++repeat){
        gaussDecline=decline;gaussOutput=aData;gaussEvents.clear();gaussHistory.clear();seed();
        ReferenceGaussStorage::xy_gaussian_blur(gaussOutput,ds,gaussInput,w,h,ss,x.data(),rx,((rx*2+1)+3)&~3,y.data(),ry,(((ry*2+1)+3)&~3)+4);Effects ea;auto ta=gaussEvents,ha=gaussHistory;
        if(gaussData||gaussOwner||g_xy_malloc_used_size)std::abort();
        gaussOutput=bData;gaussEvents.clear();gaussHistory.clear();seed();
        CandidateGaussStorage::xy_gaussian_blur(gaussOutput,ds,gaussInput,w,h,ss,x.data(),rx,((rx*2+1)+3)&~3,y.data(),ry,(((ry*2+1)+3)&~3)+4);Effects eb;
        if(!(ea==eb)||std::memcmp(aData,bData,gaussOutputCount)!=0||source!=original||ta!=gaussEvents||ha!=gaussHistory||gaussData||gaussOwner||g_xy_malloc_used_size){fprintf(stderr,"Gaussian storage mismatch call=%llu w=%d h=%d r=%d,%d\n",(unsigned long long)calls,w,h,rx,ry);std::abort();}
        for(auto* output:{&a,&b}){const size_t offset=static_cast<size_t>(aligned(*output)-output->data());for(size_t i=0;i<output->size();++i)if((i<offset||i>=offset+gaussOutputCount)&&(*output)[i]!=0xa5)std::abort();}
        ++calls;outputBytes+=a.size();scratchFields+=ha.size();observations+=ta.size();declines+=decline;
    }
}
int main(){
    for(int w:{1,2,3,4,7,8,9,15,16,17,31})for(int h:{1,2,3,4,7,8,9})for(int rx=0;rx<=7;++rx)for(int ry=0;ry<=7;++ry)for(int padding:{0,16})compare(w,h,rx,ry,padding,false);
    for(int w:{1,3,17,31})for(int h:{1,7})for(int r:{0,1,7})compare(w,h,r,7-r,16,true);
    if(!calls||!declines||!gaussOwners||gaussOwners!=gaussLoans||gaussOwners!=gaussReleases||!gaussStages)std::abort();
    printf("%llu exact Gaussian scratch calls passed (%llu output bytes including guards / %llu initialized scratch index-byte fields / %llu allocation and stage observations / %llu Rust owners, loans and releases / %llu modeled Rust declines to successful native allocation; complete pinned xy_gaussian_blur, horizontal/vertical region offsets and alignment, all stage arguments and initialized-data handoff, untouched output padding, unchanged input, repeated calls, cache accounting and errno/FP/Windows error state; filter/conversion kernels controlled and independently tested elsewhere, unwritten scratch bytes never read or compared, invalid kernels/dimensions and actual allocation failure excluded)\n",(unsigned long long)calls,(unsigned long long)outputBytes,(unsigned long long)scratchFields,(unsigned long long)observations,(unsigned long long)gaussOwners,(unsigned long long)declines);
}
