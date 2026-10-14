// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_bilinear.h"
#include <array>
#include <climits>
#include <cstdio>
static uint64_t comparisons=0,bytes_compared=0,changed=0,failed_allocations=0;
struct Outcome {
    std::array<unsigned char,8192> bytes;
    std::vector<uint64_t> events;
    std::vector<WORD> history;
};
static Outcome execute(bool candidate,int width,int height,int pitch,int xf,int yf,unsigned pattern,bool fail,int passes) {
    Outcome out;
    for(size_t i=0;i<out.bytes.size();++i) {
        const unsigned value=pattern==0 ? 0 : pattern==1 ? 255 : pattern==2 ? i : i*71+(i>>8);
        out.bytes[i]=static_cast<unsigned char>(value);
    }
    bilinearEvents.clear();bilinearHistory.clear();bilinearFail=fail;
    for(int pass=0;pass<passes;++pass) {
        if(candidate)CandidateBilinear::Bilinear(out.bytes.data()+2048,width,height,pitch,xf,yf);
        else ReferenceBilinear::Bilinear(out.bytes.data()+2048,width,height,pitch,xf,yf);
    }
    if(bilinearAllocation||bilinearOwner||g_xy_malloc_used_size)std::abort();
    out.events=bilinearEvents;out.history=bilinearHistory;return out;
}
static void fixture(int width,int height,int pitch,int xf,int yf,unsigned pattern,bool fail,int passes=1) {
    const auto original=execute(false,width,height,pitch,xf,yf,pattern,fail,passes);
    const auto candidate=execute(true,width,height,pitch,xf,yf,pattern,fail,passes);
    if(original.bytes!=candidate.bytes || original.events!=candidate.events || original.history!=candidate.history) {
        fprintf(stderr,"Bilinear mismatch w=%d h=%d pitch=%d x=%d y=%d pattern=%u fail=%d passes=%d\n",width,height,pitch,xf,yf,pattern,fail,passes);
        std::abort();
    }
    bool visible=false;
    for(size_t i=0;i<original.bytes.size();++i) {
        const unsigned value=pattern==0 ? 0 : pattern==1 ? 255 : pattern==2 ? i : i*71+(i>>8);
        const auto initial=static_cast<unsigned char>(value);
        if(i<64 || i>=original.bytes.size()-64)if(original.bytes[i]!=initial)std::abort();
        visible|=original.bytes[i]!=initial;
    }
    ++comparisons;bytes_compared+=original.bytes.size();changed+=visible;
    if(fail || width<0)++failed_allocations;
}
int main() {
    for(int width:{0,1,2,3,7,8,9,15,16,17,31,32,33,63,129,257})for(int height:{-1,0,1,2,4})
        for(int pitch:{-320,-1,0,1,320})for(int xf=0;xf<=8;++xf)for(int yf=0;yf<=8;++yf)
            for(unsigned pattern:{1U,2U,3U}) {
                fixture(width,height,pitch,xf,yf,pattern,false);
            }
    for(int width:{-1,0,1,257})for(int height:{-1,0,1,4})for(int xf:{0,1,7,8})for(int yf:{0,1,7,8})
        fixture(width,height,320,xf,yf,3,true);
    for(int xf:{INT_MIN,-1,0,8,9,INT_MAX})for(int yf:{INT_MIN,-1,0,8,9,INT_MAX})
        fixture(64,3,320,xf,yf,0,false);
    for(int pitch:{-320,-1,0,1,320})for(int xf=0;xf<=8;++xf)for(int yf=0;yf<=8;++yf)
        fixture(129,4,pitch,xf,yf,3,false,3);
    if(!bilinearOwners||bilinearOwners!=bilinearLoans||bilinearOwners!=bilinearReleases)std::abort();
    printf("%llu exact bilinear comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu injected allocation-failure cases / %llu Rust aligned scratch owners, loans and releases; every 1/8-pixel factor pair, column history, row aliasing, negative/zero pitch, repeated shifts and defined native fallback)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytes_compared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(failed_allocations),static_cast<unsigned long long>(bilinearOwners));
}
