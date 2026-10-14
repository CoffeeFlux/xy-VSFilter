// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_be_filter.h"
#include <array>
#include <cstdio>
typedef void (*Filter)(PUINT8,int,int,int);
static const Filter referenceScalar[]={ReferenceFilter::xy_be_filter_c<0>,ReferenceFilter::xy_be_filter_c<1>,ReferenceFilter::xy_be_filter_c<2>,ReferenceFilter::xy_be_filter_c<3>};
static const Filter candidateScalar[]={CandidateFilter::xy_be_filter_c<0>,CandidateFilter::xy_be_filter_c<1>,CandidateFilter::xy_be_filter_c<2>,CandidateFilter::xy_be_filter_c<3>};
#if FILTER_NATIVE_SSE
static const Filter referenceVector[]={ReferenceFilter::xy_be_filter_sse<0>,ReferenceFilter::xy_be_filter_sse<1>,ReferenceFilter::xy_be_filter_sse<2>,ReferenceFilter::xy_be_filter_sse<3>};
static const Filter candidateVector[]={CandidateFilter::xy_be_filter_sse<0>,CandidateFilter::xy_be_filter_sse<1>,CandidateFilter::xy_be_filter_sse<2>,CandidateFilter::xy_be_filter_sse<3>};
#endif
static uint64_t comparisons=0,bytesCompared=0,changed=0,vectorComparisons=0,triples=0;
template<size_t N>
static void compare(const std::array<uint8_t,N>& seed,unsigned policy,bool vector,int offset,int width,int height,int stride,int passes) {
    auto a=seed,b=seed;
    Filter reference=referenceScalar[policy],candidate=candidateScalar[policy];
#if FILTER_NATIVE_SSE
    if(vector){reference=referenceVector[policy];candidate=candidateVector[policy];}
#endif
    for(int pass=0;pass<passes;++pass) {
        reference(a.data()+offset,width,height,stride);
        candidate(b.data()+offset,width,height,stride);
    }
    if(a!=b) {
        fprintf(stderr,"Horizontal filter mismatch policy=%u vector=%d offset=%d width=%d height=%d stride=%d passes=%d\n",policy,vector,offset,width,height,stride,passes);
        std::abort();
    }
    ++comparisons;bytesCompared+=N;changed+=a!=seed;vectorComparisons+=vector;
}
int main() {
    const int modes=FILTER_NATIVE_SSE ? 2 : 1;
    std::array<uint8_t,4096> seed;
    for(unsigned pattern=0;pattern<6;++pattern) {
        for(size_t i=0;i<seed.size();++i)seed[i]=pattern==0 ? 0 : pattern==1 ? 255 : pattern==2 ? (i&1 ? 255 : 0) : static_cast<uint8_t>(i*(pattern*71)+i/17);
        for(int mode=0;mode<modes;++mode)for(unsigned policy=0;policy<4;++policy)
            for(int width:{-1,0,1,2,7,8,9,15,16,17,31,32,33,63,64,65,127,129,255,256,257})
                for(int height:{-1,0,1,3})for(int stride:{-320,-17,-1,0,1,17,320})for(int offset:{1024,1025,1031,1039})
                    for(int passes:{1,3})compare(seed,policy,mode!=0,offset,width,height,stride,passes);
    }
    // Exhaust all byte triples in a full SIMD group and its scalar tail. Each
    // implementation is compared with its own extracted reference entry point.
    std::array<uint8_t,32> small;
    small.fill(0xa7);
    for(unsigned left=0;left<256;++left)for(unsigned center=0;center<256;++center)for(unsigned right=0;right<256;++right) {
        for(int i=0;i<3;++i) {small[7+i*3]=static_cast<uint8_t>(left);small[8+i*3]=static_cast<uint8_t>(center);small[9+i*3]=static_cast<uint8_t>(right);}
        for(unsigned policy=0;policy<4;++policy)for(int mode=0;mode<modes;++mode) {
            compare(small,policy,mode!=0,7,9,1,0,1);++triples;
        }
    }
    // The policy names do not justify substituting a standard rounding rule.
    const std::array<uint8_t,3> control={{1,3,0}};
    auto down=control,up=control,even=control;
    referenceScalar[0](down.data(),3,1,0);referenceScalar[2](up.data(),3,1,0);referenceScalar[3](even.data(),3,1,0);
    if(down==up || up==even){fprintf(stderr,"Rounding policy control failed\n");return 1;}
    printf("%llu exact horizontal-filter comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu actual SSE comparisons, %llu exhaustive triple/policy/path cases; all rounding policies, overlapping rows, negative/zero stride and repeated passes)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytesCompared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(vectorComparisons),static_cast<unsigned long long>(triples));
}
