// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_byte_float.h"
#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
typedef void (*Filter)(float*,int,int,const UINT8*,int,int,int);
struct Buffer {alignas(16) std::array<float,4096> words;};
static uint64_t comparisons=0,bytesCompared=0,changed=0,vectorComparisons=0,aliasCases=0;
static void compare(const Buffer& seed,bool vector,int sourceOffset,int destinationOffset,int width,int height,int sourceStride,int destinationWidth,int destinationStride,int passes) {
    Buffer a,b;std::memcpy(&a,&seed,sizeof(a));std::memcpy(&b,&seed,sizeof(b));
    Filter reference=ReferenceByteFloat::xy_byte_2_float_c,candidate=CandidateByteFloat::xy_byte_2_float_c;
#if BYTE_FLOAT_NATIVE_SSE
    if(vector){reference=ReferenceByteFloat::xy_byte_2_float_sse;candidate=CandidateByteFloat::xy_byte_2_float_sse;}
#endif
    for(int pass=0;pass<passes;++pass) {
        std::feclearexcept(FE_ALL_EXCEPT);
        reference(a.words.data()+destinationOffset,destinationWidth,destinationStride*4,reinterpret_cast<const UINT8*>(a.words.data())+sourceOffset,width,height,sourceStride);
        const int referenceFlags=std::fetestexcept(FE_ALL_EXCEPT);
        std::feclearexcept(FE_ALL_EXCEPT);
        candidate(b.words.data()+destinationOffset,destinationWidth,destinationStride*4,reinterpret_cast<const UINT8*>(b.words.data())+sourceOffset,width,height,sourceStride);
        const int candidateFlags=std::fetestexcept(FE_ALL_EXCEPT);
        if(referenceFlags!=candidateFlags || std::memcmp(&a,&b,sizeof(a))) {
            fprintf(stderr,"Byte-float mismatch vector=%d srcByte=%d dstWord=%d width=%d height=%d srcStride=%d dstWidth=%d dstStrideWords=%d pass=%d flags=%d/%d\n",vector,sourceOffset,destinationOffset,width,height,sourceStride,destinationWidth,destinationStride,pass,referenceFlags,candidateFlags);std::abort();
        }
    }
    if(std::memcmp(a.words.data(),seed.words.data(),64) || std::memcmp(a.words.data()+a.words.size()-16,seed.words.data()+seed.words.size()-16,64))std::abort();
    ++comparisons;bytesCompared+=sizeof(a);changed+=std::memcmp(&a,&seed,sizeof(a))!=0;vectorComparisons+=vector;aliasCases+=sourceOffset>=destinationOffset*4-256 && sourceOffset<=destinationOffset*4+256;
}
int main() {
    const int modes=BYTE_FLOAT_NATIVE_SSE ? 2 : 1;
    Buffer seed;
    for(int pattern=0;pattern<3;++pattern) {
        UINT8* bytes=reinterpret_cast<UINT8*>(seed.words.data());
        for(size_t i=0;i<sizeof(seed);++i)bytes[i]=pattern==0 ? 0 : pattern==1 ? 255 : static_cast<UINT8>(i);
        for(int mode=0;mode<modes;++mode)for(int width:{-1,0,1,2,7,15,16,17,31,32,33})for(int height:{-1,0,1,2,5})for(int sourceStride:{-64,-1,0,1,8,64}) {
            if(height<0 && sourceStride<0)continue;
            for(int destinationWidth:{-1,0,1,7,32,40})for(int destinationStride:{-64,-1,0,1,16,64})for(int offset:{0,1})for(int passes:{1,2}) {
                if(mode && ((destinationStride&3) || offset))continue;
                compare(seed,mode!=0,512+offset,2048+offset,width,height,sourceStride,destinationWidth,destinationStride,passes);
            }
        }
        for(int mode=0;mode<modes;++mode)for(int width:{1,15,16,17,33,65})for(int height:{1,3,5})for(int sourceStride:{1,17,64})for(int destinationStride:{-16,0,16,64})for(int delta:{-33,-17,-1,0,1,3,17,33})
            compare(seed,mode!=0,4096+delta,1024,width,height,sourceStride,32,destinationStride,2);
        for(int mode=0;mode<modes;++mode)for(int value=0;value<256;++value) {
            std::memset(bytes+512,value,2048);
            for(int width:{15,16,17,63,64,65,255,256,257})
                compare(seed,mode!=0,512,2048,width,5,320,width+7,288,2);
        }
    }
#if BYTE_FLOAT_NATIVE_SSE
    bool different=false;
    for(int delta:{-17,-1,0,1,17}) {
        Buffer scalar,simd;std::memcpy(&scalar,&seed,sizeof(seed));std::memcpy(&simd,&seed,sizeof(seed));
        ReferenceByteFloat::xy_byte_2_float_c(scalar.words.data()+1024,32,128,reinterpret_cast<const UINT8*>(scalar.words.data())+4096+delta,33,3,64);
        ReferenceByteFloat::xy_byte_2_float_sse(simd.words.data()+1024,32,128,reinterpret_cast<const UINT8*>(simd.words.data())+4096+delta,33,3,64);
        different|=std::memcmp(&scalar,&simd,sizeof(scalar))!=0;
    }
    if(!different){fprintf(stderr,"Byte-float grouped-source alias control failed\n");return 1;}
#endif
    if(!byteFloatHandled || !byteFloatFallback){fprintf(stderr,"Byte-float route controls failed\n");return 1;}
    printf("%llu exact byte-to-float comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu actual SSE comparisons, %llu nearby/alias cases, %llu Rust-handled calls, %llu native fallback calls; all byte values, floating-point exception flags, padding, overlapping rows and scalar/SSE source grouping)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytesCompared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(vectorComparisons),static_cast<unsigned long long>(aliasCases),static_cast<unsigned long long>(byteFloatHandled),static_cast<unsigned long long>(byteFloatFallback));
}
