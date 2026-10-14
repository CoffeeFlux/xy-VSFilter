// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_be_blur.h"
#include <array>
#include <cstdio>
static uint64_t comparisons=0,bytes_compared=0,changed=0,vector_comparisons=0,failures=0;
struct Outcome {
    std::array<unsigned char,8192> bytes;
    std::array<unsigned,8> unused;
    std::vector<uint64_t> events;
    std::vector<WORD> history;
};
static unsigned char initial(size_t i,unsigned pattern) {
    if(pattern==0)return 0;
    if(pattern==1)return 255;
    if(pattern==2)return static_cast<unsigned char>(i);
    if(pattern==3)return static_cast<unsigned char>(i*71+(i>>8));
    if(pattern==4)return i&1 ? 255 : 0;
    return static_cast<unsigned char>((i*0x13579U)^(i>>3)^(pattern*43));
}
static Outcome execute(bool candidate,bool vector,int width,int height,int pitch,unsigned pattern,int fail,int passes) {
    Outcome out;
    for(size_t i=0;i<out.bytes.size();++i)out.bytes[i]=initial(i,pattern);
    out.unused.fill(0x79c135e7U);
    blurAllocations.clear();blurEvents.clear();blurHistory.clear();blurFail=fail;
    for(int pass=0;pass<passes;++pass) {
#if BLUR_NATIVE_SSE
        if(vector) {
            if(candidate)CandidateBlur::be_blur(out.bytes.data()+2048,out.unused.data(),width,height,pitch);
            else ReferenceBlur::be_blur(out.bytes.data()+2048,out.unused.data(),width,height,pitch);
        }else
#endif
        {
            if(candidate)CandidateBlur::be_blur_c(out.bytes.data()+2048,out.unused.data(),width,height,pitch);
            else ReferenceBlur::be_blur_c(out.bytes.data()+2048,out.unused.data(),width,height,pitch);
        }
    }
    // Record outstanding fixture-owned allocations without reading their
    // possibly unwritten contents; release them outside the observed call.
    for(size_t i=0;i<blurAllocations.size();++i)if(blurAllocations[i].pointer) {
        blurEvents.push_back(2);blurEvents.push_back(i);
        if(blurAllocations[i].rust)std::abort();g_xy_malloc_used_size-=blurAllocations[i].size;std::free(blurAllocations[i].pointer);blurAllocations[i].pointer=nullptr;
    }
    if(g_xy_malloc_used_size)std::abort();
    out.events=blurEvents;out.history=blurHistory;return out;
}
static void fixture(bool vector,int width,int height,int pitch,unsigned pattern,int fail=-1,int passes=1) {
    const auto a=execute(false,vector,width,height,pitch,pattern,fail,passes);
    const auto b=execute(true,vector,width,height,pitch,pattern,fail,passes);
    if(a.bytes!=b.bytes || a.unused!=b.unused || a.events!=b.events || a.history!=b.history) {
        fprintf(stderr,"Blur mismatch vector=%d w=%d h=%d pitch=%d pattern=%u fail=%d passes=%d\n",vector,width,height,pitch,pattern,fail,passes);std::abort();
    }
    bool visible=false;
    for(size_t i=0;i<a.bytes.size();++i) {
        if(i<64 || i>=a.bytes.size()-64)if(a.bytes[i]!=initial(i,pattern))std::abort();
        visible|=a.bytes[i]!=initial(i,pattern);
    }
    for(unsigned value:a.unused)if(value!=0x79c135e7U)std::abort();
    ++comparisons;bytes_compared+=a.bytes.size();changed+=visible;vector_comparisons+=vector;failures+=fail>=0;
}
int main() {
    const int modes=BLUR_NATIVE_SSE ? 2 : 1;
    for(int mode=0;mode<modes;++mode) {
        for(int width:{0,1,2,3,4,9,10,11,17,18,19,25,26,27,33,34,35,65,129,257})
            for(int height:{-1,0,1,2,3,4,5})for(int pitch:{-320,-17,-9,-8,-2,-1,0,1,2,8,9,17,320})
                for(unsigned pattern=0;pattern<8;++pattern)for(int passes:{1,2,3})
                    fixture(mode!=0,width,height,pitch,pattern,-1,passes);
        for(int width:{-1,0,1,2,17,257})for(int height:{0,1,2,3,5})for(int pitch:{-320,0,320})for(int fail:{0,1})
            fixture(mode!=0,width,height,pitch,3,fail);
    }
#if BLUR_NATIVE_SSE
    bool different=false;
    for(int width:{10,11,18,19,26,27})for(int pitch:{-9,-2,0}) {
        const auto scalar=execute(false,false,width,4,pitch,3,-1,1);
        const auto simd=execute(false,true,width,4,pitch,3,-1,1);
        different|=scalar.bytes!=simd.bytes;
    }
    if(!different){fprintf(stderr,"Blur grouped-read alias control failed\n");return 1;}
#endif
    if(!blurOwners||blurOwners!=blurLoans||blurOwners!=blurReleases)std::abort();
    printf("%llu exact fixed-blur comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu actual SSE comparisons, %llu native-fallback allocation-failure cases / %llu Rust scratch owners, loans and releases; column histories, scalar/SSE boundaries, overlapping rows, negative/zero stride and repeated passes)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytes_compared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(vector_comparisons),static_cast<unsigned long long>(failures),static_cast<unsigned long long>(blurOwners));
}
