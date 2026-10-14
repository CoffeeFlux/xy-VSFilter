// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_transpose.h"
#include <array>
#include <cstdio>
static uint64_t comparisons=0,bytesCompared=0,changed=0,aliasCases=0;
typedef std::array<UINT8,8192> Buffer;
static void compare(const Buffer& seed,int sourceOffset,int destinationOffset,int width,int height,int sourceStride,int destinationWidth,int destinationStride,int passes) {
    auto a=seed,b=seed;
    for(int pass=0;pass<passes;++pass) {
        ReferenceTranspose::xy_byte_2_byte_transpose_c(a.data()+destinationOffset,destinationWidth,destinationStride,a.data()+sourceOffset,width,height,sourceStride);
        CandidateTranspose::xy_byte_2_byte_transpose_c(b.data()+destinationOffset,destinationWidth,destinationStride,b.data()+sourceOffset,width,height,sourceStride);
    }
    if(a!=b) {
        fprintf(stderr,"Byte transpose mismatch src=%d dst=%d width=%d height=%d srcStride=%d dstWidth=%d dstStride=%d passes=%d\n",sourceOffset,destinationOffset,width,height,sourceStride,destinationWidth,destinationStride,passes);std::abort();
    }
    for(size_t i=0;i<64;++i)if(a[i]!=seed[i] || a[a.size()-1-i]!=seed[a.size()-1-i])std::abort();
    ++comparisons;bytesCompared+=a.size();changed+=a!=seed;aliasCases+=sourceOffset>=destinationOffset-256 && sourceOffset<=destinationOffset+256;
}
int main() {
    Buffer seed;
    for(int pattern=0;pattern<3;++pattern) {
        for(size_t i=0;i<seed.size();++i)seed[i]=pattern==0 ? 0 : pattern==1 ? 255 : static_cast<UINT8>(i*71+i/17);
        for(int width:{-1,0,1,2,7,8,9,16,17,33})for(int height:{-1,0,1,2,3,5})for(int sourceStride:{-64,-1,0,1,8,16,64}) {
            // Original negative-height/negative-stride traversal does not end.
            if(height<0 && sourceStride<0)continue;
            for(int destinationWidth:{-1,0,1,3,5,7})for(int destinationStride:{-64,-1,0,1,8,16,64})for(int offset:{0,1})for(int passes:{1,2})
                compare(seed,1024+offset,4096+offset,width,height,sourceStride,destinationWidth,destinationStride,passes);
        }
        for(int width:{1,2,9,17,33})for(int height:{1,3,5})for(int sourceStride:{1,8,64})for(int destinationWidth:{1,5,7})for(int destinationStride:{-16,0,1,7,16})for(int delta:{-33,-17,-1,0,1,3,17,33})
            compare(seed,2048,2048+delta,width,height,sourceStride,destinationWidth,destinationStride,2);
        for(int width:{63,64,65,127,128,129,255,256,257})for(int height:{1,3,5})for(int extra:{0,1,7})
            compare(seed,512,4096,width,height,width+extra,height+extra,height+extra+1,2);
    }
    // Prove that source/destination aliases need the original incremental path.
    bool different=false;
    for(int delta:{-17,-1,0,1,17}) {
        auto shared=seed,snapshot=seed;const auto input=seed;
        ReferenceTranspose::xy_byte_2_byte_transpose_c(shared.data()+2048+delta,5,7,shared.data()+2048,17,5,32);
        ReferenceTranspose::xy_byte_2_byte_transpose_c(snapshot.data()+2048+delta,5,7,input.data()+2048,17,5,32);
        different|=shared!=snapshot;
    }
    if(!different || !transposeHandled || !transposeFallback){fprintf(stderr,"Transpose route/alias controls failed\n");return 1;}
    printf("%llu exact byte-transpose comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases, %llu nearby/alias cases, %llu Rust-handled calls, %llu native fallback calls; zero extension, pitch gaps, repeated calls and original unusual-dimension traversal)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytesCompared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(aliasCases),static_cast<unsigned long long>(transposeHandled),static_cast<unsigned long long>(transposeFallback));
}
