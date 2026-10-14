// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_be_filter.h"
#include <array>
#include <cstdio>
typedef void (*Filter)(PUINT8,int,int,int,PCUINT);
static const Filter referenceScalar[]={ReferenceFilter::xy_be_filter2_c<0>,ReferenceFilter::xy_be_filter2_c<1>,ReferenceFilter::xy_be_filter2_c<2>,ReferenceFilter::xy_be_filter2_c<3>};
static const Filter candidateScalar[]={CandidateFilter::xy_be_filter2_c<0>,CandidateFilter::xy_be_filter2_c<1>,CandidateFilter::xy_be_filter2_c<2>,CandidateFilter::xy_be_filter2_c<3>};
#if FILTER_NATIVE_SSE
static const Filter referenceVector[]={ReferenceFilter::xy_be_filter2_sse<0>,ReferenceFilter::xy_be_filter2_sse<1>,ReferenceFilter::xy_be_filter2_sse<2>,ReferenceFilter::xy_be_filter2_sse<3>};
static const Filter candidateVector[]={CandidateFilter::xy_be_filter2_sse<0>,CandidateFilter::xy_be_filter2_sse<1>,CandidateFilter::xy_be_filter2_sse<2>,CandidateFilter::xy_be_filter2_sse<3>};
#endif
static uint64_t comparisons=0,bytesCompared=0,changed=0,vectorComparisons=0,aliases=0,combinations=0;
template<size_t N>
static void compare(std::array<UINT,N> seed,unsigned policy,bool vector,int offset,int width,int height,int stride,int passes,UINT side,UINT middle,int coefficientIndex=-1) {
    const UINT coefficients[]={side,middle,side};
    if(coefficientIndex>=0)for(int i=0;i<3;++i)seed[coefficientIndex+i]=coefficients[i];
    auto a=seed,b=seed;
    Filter reference=referenceScalar[policy],candidate=candidateScalar[policy];
#if FILTER_NATIVE_SSE
    if(vector){reference=referenceVector[policy];candidate=candidateVector[policy];}
#endif
    for(int pass=0;pass<passes;++pass) {
        reference(reinterpret_cast<PUINT8>(a.data())+offset,width,height,stride,coefficientIndex<0 ? coefficients : a.data()+coefficientIndex);
        candidate(reinterpret_cast<PUINT8>(b.data())+offset,width,height,stride,coefficientIndex<0 ? coefficients : b.data()+coefficientIndex);
    }
    if(a!=b) {
        fprintf(stderr,"Weighted filter mismatch policy=%u vector=%d offset=%d width=%d height=%d stride=%d passes=%d side=%u middle=%u coefficientIndex=%d\n",policy,vector,offset,width,height,stride,passes,side,middle,coefficientIndex);
        std::abort();
    }
    ++comparisons;bytesCompared+=sizeof(a);changed+=a!=seed;vectorComparisons+=vector;aliases+=coefficientIndex>=0;
}
int main() {
    const int modes=FILTER_NATIVE_SSE ? 2 : 1;
    std::array<UINT,512> seed;
    const UINT pairs[][2]={{0,0},{0,256},{1,254},{63,130},{64,0},{64,128},{256,256},{0xffffffffU,0x80000000U}};
    for(unsigned pattern=0;pattern<4;++pattern) {
        PUINT8 bytes=reinterpret_cast<PUINT8>(seed.data());
        for(size_t i=0;i<sizeof(seed);++i)bytes[i]=pattern==0 ? 0 : pattern==1 ? 255 : static_cast<UINT8>(i*(pattern*71)+i/17);
        for(int mode=0;mode<modes;++mode)for(unsigned policy=0;policy<4;++policy)for(const auto& pair:pairs)
            for(int width:{-1,0,1,2,7,8,9,15,16,17,31,32,33,65,257})for(int height:{-1,0,1,3})for(int stride:{-256,-9,-1,0,1,9,256}) {
                // A negative height and stride make the original SSE loop walk
                // away from its termination pointer. That is not a parity case.
                if(mode && height<0 && stride<0)continue;
                for(int offset:{768,775,783})for(int passes:{1,3})
                    compare(seed,policy,mode!=0,offset,width,height,stride,passes,pair[0],pair[1]);
            }
        for(int mode=0;mode<modes;++mode)for(unsigned policy=0;policy<4;++policy)for(const auto& pair:pairs)
            for(int width:{1,8,9,17,33,65})for(int stride:{-9,0,1,9,64})for(int index:{191,192,193,194,195,196,199,208})
                compare(seed,policy,mode!=0,768,width,3,stride,3,pair[0],pair[1],index);
    }
    const UINT wide[]={0,1,63,64,65,127,128,129,255,256,257,32767,32768,65535,65536,0x7fffffffU,0x80000000U,0xffffffffU};
    for(UINT side:wide)for(UINT middle:wide)for(unsigned policy=0;policy<4;++policy)for(int mode=0;mode<modes;++mode)
        for(int width:{1,8,9,17,65})compare(seed,policy,mode!=0,768,width,3,64,2,side,middle);
    // For every normalized filter, exhaust the scalar weighted sum's complete
    // neighboring-byte sum and center-byte domain, also inside actual SSE groups.
    std::array<UINT,8> small;small.fill(0xa7a7a7a7U);
    PUINT8 pixels=reinterpret_cast<PUINT8>(small.data());
    for(UINT side=0;side<=64;++side)for(UINT neighbors=0;neighbors<=510;++neighbors)for(UINT center=0;center<256;++center) {
        for(int i=0;i<3;++i) {pixels[7+i*3]=static_cast<UINT8>(neighbors/2);pixels[8+i*3]=static_cast<UINT8>(center);pixels[9+i*3]=static_cast<UINT8>(neighbors-neighbors/2);}
        for(unsigned policy=0;policy<4;++policy)for(int mode=0;mode<modes;++mode) {
            compare(small,policy,mode!=0,7,9,1,16,1,side,256-2*side);++combinations;
        }
    }
#if FILTER_NATIVE_SSE
    const UINT zero[]={0,0,0};auto scalar=small,simd=small;
    referenceScalar[0](reinterpret_cast<PUINT8>(scalar.data())+7,9,1,16,zero);
    referenceVector[0](reinterpret_cast<PUINT8>(simd.data())+7,9,1,16,zero);
    if(scalar==simd){fprintf(stderr,"Scalar coefficient-zero shortcut control failed\n");return 1;}
#endif
    printf("%llu exact weighted-filter comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu actual SSE comparisons, %llu coefficient-alias cases, %llu exhaustive sum/center/coefficient/policy/path cases; scalar shortcuts, wide coefficients, saturation, scalar tails and row traversal)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytesCompared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(vectorComparisons),static_cast<unsigned long long>(aliases),static_cast<unsigned long long>(combinations));
}
