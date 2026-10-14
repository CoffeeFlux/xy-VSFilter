// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_transpose.h"
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
typedef void (*Filter)(float*,int,int,const float*,int,int,int);
struct Buffer {alignas(16) std::array<float,4096> words;};
static uint64_t comparisons=0,bytesCompared=0,changed=0,vectorComparisons=0,aliasCases=0;
static void compare(const Buffer& seed,bool vector,int sourceOffset,int destinationOffset,int width,int height,int sourceStride,int destinationWidth,int destinationStride,int passes) {
    Buffer a,b;std::memcpy(&a,&seed,sizeof(a));std::memcpy(&b,&seed,sizeof(b));
    Filter reference=ReferenceTranspose::xy_float_2_float_transpose_c,candidate=CandidateTranspose::xy_float_2_float_transpose_c;
#if TRANSPOSE_NATIVE_SSE
    if(vector){reference=ReferenceTranspose::xy_float_2_float_transpose_sse;candidate=CandidateTranspose::xy_float_2_float_transpose_sse;}
#endif
    for(int pass=0;pass<passes;++pass) {
        std::feclearexcept(FE_ALL_EXCEPT);
        reference(a.words.data()+destinationOffset,destinationWidth,destinationStride*4,a.words.data()+sourceOffset,width,height,sourceStride*4);
        const int referenceFlags=std::fetestexcept(FE_ALL_EXCEPT);
        std::feclearexcept(FE_ALL_EXCEPT);
        candidate(b.words.data()+destinationOffset,destinationWidth,destinationStride*4,b.words.data()+sourceOffset,width,height,sourceStride*4);
        const int candidateFlags=std::fetestexcept(FE_ALL_EXCEPT);
        if(referenceFlags!=candidateFlags || std::memcmp(&a,&b,sizeof(a))) {
            fprintf(stderr,"Float transpose mismatch vector=%d src=%d dst=%d width=%d height=%d srcStride=%d dstWidth=%d dstStride=%d pass=%d flags=%d/%d\n",vector,sourceOffset,destinationOffset,width,height,sourceStride,destinationWidth,destinationStride,pass,referenceFlags,candidateFlags);std::abort();
        }
    }
    if(std::memcmp(a.words.data(),seed.words.data(),64) || std::memcmp(a.words.data()+a.words.size()-16,seed.words.data()+seed.words.size()-16,64))std::abort();
    ++comparisons;bytesCompared+=sizeof(a);changed+=std::memcmp(&a,&seed,sizeof(a))!=0;vectorComparisons+=vector;aliasCases+=sourceOffset>=destinationOffset-256 && sourceOffset<=destinationOffset+256;
}
int main() {
    const int modes=TRANSPOSE_NATIVE_SSE ? 2 : 1;
    const uint32_t special[]={0,0x80000000U,1,0x80000001U,0x007fffffU,0x807fffffU,0x7f800000U,0xff800000U,0x7f800001U,0xff800001U,0x7fc01234U,0xffc01234U,0x00800000U,0x7f7fffffU};
    Buffer seed;
    for(int pattern=0;pattern<4;++pattern) {
        for(size_t i=0;i<seed.words.size();++i) {
            const uint32_t bits=pattern==0 ? (i&1 ? 0x80000000U : 0) : pattern==1 ? (static_cast<uint32_t>(i%254+1)<<23) | (static_cast<uint32_t>(i*71)&0x807fffffU) : pattern==2 ? special[i%(sizeof(special)/sizeof(special[0]))] : static_cast<uint32_t>(i*0x13579bdfU)^(static_cast<uint32_t>(i)>>3);
            std::memcpy(&seed.words[i],&bits,sizeof(bits));
        }
        for(int mode=0;mode<modes;++mode)for(int width:{-1,0,1,2,3,4,7,8,9,17})for(int height:{-1,0,1,2,3,4,5,7,8,9})for(int sourceStride:{-32,-1,0,1,8,32}) {
            if(height<0 && sourceStride<0)continue;
            for(int destinationWidth:{-1,0,1,3,9,12})for(int destinationStride:{-32,-1,0,1,12,32})for(int offset:{0,1})for(int passes:{1,2}) {
                if(mode && ((destinationStride&3) || offset))continue;
                compare(seed,mode!=0,512+offset,2048+offset,width,height,sourceStride,destinationWidth,destinationStride,passes);
            }
        }
        for(int mode=0;mode<modes;++mode)for(int width:{1,3,4,9,17})for(int height:{1,3,4,5,8})for(int sourceStride:{1,8,32})for(int destinationStride:{-16,0,4,8,16})for(int delta:{-32,-16,0,4,16,32})
            compare(seed,mode!=0,1024,1024+delta,width,height,sourceStride,12,destinationStride,2);
        for(int mode=0;mode<modes;++mode)for(int width:{31,32,33,63,64,65,127,128,129})for(int height:{1,3,4,5,8})
            compare(seed,mode!=0,512,2048,width,height,width+3,8,8,2);
    }
    if(!wordTransposeHandled || !wordTransposeFallback){fprintf(stderr,"Float transpose route controls failed\n");return 1;}
    printf("%llu exact float-transpose comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu actual SSE comparisons, %llu nearby/alias cases, %llu Rust-handled calls, %llu native fallback calls; float bit patterns and exception flags, zero extension, pitch gaps and original unusual-dimension traversal)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytesCompared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(vectorComparisons),static_cast<unsigned long long>(aliasCases),static_cast<unsigned long long>(wordTransposeHandled),static_cast<unsigned long long>(wordTransposeFallback));
}
