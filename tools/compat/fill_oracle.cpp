// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_hdmv.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#if HDMV_NATIVE_PIXELS
// The pinned planar SSE helper has different Clang Release/Debug results.
// Only MSVC Release (the production compiler) or the original Debug branch is
// an admitted planar reference. Do not count Clang Release as matching.
#if defined(_MSC_VER) || defined(_DEBUG)
#define XY_PLANAR_REFERENCE 1
#else
#define XY_PLANAR_REFERENCE 0
#endif
static uint64_t planar_comparisons=0;
static uint64_t comparisons=0,bytes_compared=0,changed=0,rectangles=0;
template<class Storage> static void equal(const Storage& initial,const Storage& a,const Storage& b,const char* kind) {
    if(a!=b){fprintf(stderr,"Fill mismatch %s at comparison %llu\n",kind,static_cast<unsigned long long>(comparisons)); for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]){fprintf(stderr,"first byte %llu: initial=%u reference=%u candidate=%u\n",static_cast<unsigned long long>(i),initial[i],a[i],b[i]);break;}exit(1);}
    for(size_t i=0;i<64;++i)if(a[i]!=initial[i] || a[a.size()-1-i]!=initial[a.size()-1-i]) {
        fprintf(stderr,"Fill fixture changed outer guard\n");exit(1);
    }
    ++comparisons;bytes_compared+=a.size();if(a!=initial)++changed;
}
static void row(int mode,int width,int offset,BYTE alpha,DWORD color) {
    std::vector<BYTE> initial(1408);
    for(size_t i=0;i<initial.size();++i)initial[i]=static_cast<BYTE>((mode==0 || mode==3) ? i/4+(i%4)*47 : i);
    auto a=initial,b=initial;
    BYTE* pa=a.data()+128+offset;BYTE* pb=b.data()+128+offset;
    if(mode==0) {
        ReferencePixels::packed_pix_mix_sse2(pa,alpha,width,color);
        CandidatePixels::packed_pix_mix_sse2(pb,alpha,width,color);
    }else if(mode==3) {
        ReferencePixels::packed_pix_mix_c(pa,alpha,width,color);
        CandidatePixels::packed_pix_mix_c(pb,alpha,width,color);
    }else if(mode==1) {
        ReferencePixels::AlphaBlt8bppSingleAlphaSse2(pa,alpha,static_cast<BYTE>(color),1,width,0);
        CandidatePixels::AlphaBlt8bppSingleAlphaSse2(pb,alpha,static_cast<BYTE>(color),1,width,0);
    }else {
        ReferencePixels::AlphaBlt8bppSingleAlphaC(pa,alpha,static_cast<BYTE>(color),1,width,0);
        CandidatePixels::AlphaBlt8bppSingleAlphaC(pb,alpha,static_cast<BYTE>(color),1,width,0);
    }
    equal(initial,a,b,"row");
}
static void masked(int mode,int width,int offset,int delta,DWORD color,unsigned salt) {
    std::vector<BYTE> initial(1408);
    for(size_t i=0;i<initial.size();++i)initial[i]=static_cast<BYTE>(i*73+salt+(i>>8));
    auto a=initial,b=initial;
    BYTE* pa=a.data()+128+offset;BYTE* pb=b.data()+128+offset;
    std::vector<BYTE> mask(512);
    for(size_t i=0;i<mask.size();++i)mask[i]=static_cast<BYTE>(i*37+salt);
    const BYTE* ma=delta==INT_MAX ? mask.data()+32 : pa+delta;
    const BYTE* mb=delta==INT_MAX ? mask.data()+32 : pb+delta;
    if(mode==1) {
        ReferencePixels::packed_pix_mix_sse2(pa,ma,width,color);
        CandidatePixels::packed_pix_mix_sse2(pb,mb,width,color);
    }else if(mode==2) {
        ReferencePixels::packed_pix_add_c(pa,ma,width,color);
        CandidatePixels::packed_pix_add_c(pb,mb,width,color);
    }else if(mode==3) {
        ReferencePixels::packed_pix_add_sse2(pa,ma,width,color);
        CandidatePixels::packed_pix_add_sse2(pb,mb,width,color);
    }else {
        ReferencePixels::packed_pix_mix_c(pa,ma,width,color);
        CandidatePixels::packed_pix_mix_c(pb,mb,width,color);
    }
    equal(initial,a,b,"masked packed");
}
static void additive_exhaustive(bool simd,BYTE alpha,BYTE value) {
    std::vector<BYTE> initial(1152),mask(256,alpha);
    for(size_t i=0;i<initial.size();++i)initial[i]=static_cast<BYTE>(i/4+(i%4)*47);
    auto a=initial,b=initial;
    const DWORD color=(static_cast<DWORD>(alpha^value)<<24)|value*0x010101u;
    if(simd) {
        ReferencePixels::packed_pix_add_sse2(a.data()+64,mask.data(),256,color);
        CandidatePixels::packed_pix_add_sse2(b.data()+64,mask.data(),256,color);
    }else {
        ReferencePixels::packed_pix_add_c(a.data()+64,mask.data(),256,color);
        CandidatePixels::packed_pix_add_c(b.data()+64,mask.data(),256,color);
    }
    equal(initial,a,b,"additive channel combinations");
}
static void plane(bool simd,int width,int height,int offset,int delta,int src_stride,int dst_stride,BYTE value,unsigned salt,int fixed_alpha=-1) {
    // Alignment is controlled independently of the platform's vector allocator.
    alignas(16) std::array<BYTE,4096> initial,mask,a,b;
    for(size_t i=0;i<initial.size();++i) {
        initial[i]=fixed_alpha<0 ? static_cast<BYTE>(i*73+salt+(i>>8)) : static_cast<BYTE>(i);
        mask[i]=fixed_alpha<0 ? static_cast<BYTE>(i*37+salt) : static_cast<BYTE>(fixed_alpha);
    }
    a=initial;b=initial;
    BYTE* pa=a.data()+1536+offset;BYTE* pb=b.data()+1536+offset;
    const BYTE* ma=delta==INT_MAX ? mask.data()+1536+offset : pa+delta;
    const BYTE* mb=delta==INT_MAX ? mask.data()+1536+offset : pb+delta;
    if(simd) {
        ReferencePixels::AlphaBlt8bppSse2(pa,ma,value,height,width,src_stride,dst_stride);
        CandidatePixels::AlphaBlt8bppSse2(pb,mb,value,height,width,src_stride,dst_stride);
    }else {
        ReferencePixels::AlphaBlt8bppC(pa,ma,value,height,width,src_stride,dst_stride);
        CandidatePixels::AlphaBlt8bppC(pb,mb,value,height,width,src_stride,dst_stride);
    }
    equal(initial,a,b,"masked planar");++planar_comparisons;
}
static void alias_control() {
    std::vector<BYTE> a(2048),b;
    for(size_t i=0;i<a.size();++i)a[i]=static_cast<BYTE>(i*37);
    b=a;
    ReferencePixels::packed_pix_mix_c(a.data()+128,a.data()+128,32,0x1200ff00);
    ReferencePixels::packed_pix_mix_sse2(b.data()+128,b.data()+128,32,0x1200ff00);
    if(a==b){fprintf(stderr,"Aliased-alpha fixture did not distinguish native read grouping\n");exit(1);}
    for(size_t i=0;i<a.size();++i)a[i]=static_cast<BYTE>(i*37);
    b=a;
    ReferencePixels::packed_pix_add_c(a.data()+128,a.data()+128,32,0x12793be1);
    ReferencePixels::packed_pix_add_sse2(b.data()+128,b.data()+128,32,0x12793be1);
    if(a==b){fprintf(stderr,"Additive alias grouping control failed\n");exit(1);}
    #if XY_PLANAR_REFERENCE
    alignas(16) BYTE left[128],right[128];
    for(int i=0;i<128;++i)left[i]=right[i]=static_cast<BYTE>(i*37);
    ReferencePixels::AlphaBlt8bppC(left+16,left+15,173,1,64,0,0);
    ReferencePixels::AlphaBlt8bppSse2(right+16,right+15,173,1,64,0,0);
    if(!memcmp(left,right,128)){fprintf(stderr,"Planar alias grouping control failed\n");exit(1);}
    #endif
}
static void rectangle(bool planar,int pitch,int width,int height,int x,int y,DWORD color) {
    // Extra initialized storage also permits defined overlapping rows/planes:
    // the reference performs its operations in order without clipping to pitch.
    std::vector<BYTE> initial(64+16384+64);
    for(size_t i=0;i<initial.size();++i)initial[i]=static_cast<BYTE>(i*37+(i>>8));
    auto a=initial,b=initial;
    SubPicDesc left;left.type=planar ? MSP_AYUV_PLANAR : 0;left.h=8;left.pitch=pitch;
    const int start=pitch>=0 ? 64 : 64+((planar ? 4 : 1)*left.h-1)*-pitch;
    left.bits=a.data()+start;SubPicDesc right=left;right.bits=b.data()+start;
    ReferencePixels::Rasterizer::FillSolidRect(left,x,y,width,height,color);
    CandidatePixels::Rasterizer::FillSolidRect(right,x,y,width,height,color);
    equal(initial,a,b,"rectangle");++rectangles;
}
#endif
int main() {
#if HDMV_NATIVE_PIXELS
    // Every alpha/source/destination byte combination. Packed rows contain
    // every destination value independently in each of the four channels.
    for(unsigned alpha=0;alpha<256;++alpha)for(unsigned value=0;value<256;++value) {
        const DWORD color=((alpha^value)<<24)|(value*0x010101u);
        row(0,256,(alpha^value)&15,static_cast<BYTE>(alpha),color);
        row(1,256,(alpha+value)&15,static_cast<BYTE>(alpha),color);
        row(3,256,((alpha+value)&3)*4,static_cast<BYTE>(alpha),color);
        additive_exhaustive(false,static_cast<BYTE>(alpha),static_cast<BYTE>(value));
        additive_exhaustive(true,static_cast<BYTE>(alpha),static_cast<BYTE>(value));
        #if XY_PLANAR_REFERENCE
        plane(true,256,1,(alpha+value)&15,INT_MAX,0,0,static_cast<BYTE>(value),0,alpha);
        #endif
    }
    for(int mode:{0,1,2,3})for(int width:{-1,0,1,2,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257})
        for(int offset=0;offset<16;++offset)for(unsigned alpha:{0u,1u,2u,63u,127u,128u,129u,253u,254u,255u})
            for(DWORD color:{0u,0xffffffffu,0x12345678u,0xff00ff00u,0xabcdef12u,0x00000001u,0x000000ffu}) {
                // Scalar DWORD tails use naturally aligned native accesses.
                // Complete SIMD blocks additionally cover unaligned addresses.
                if(width>0 && offset%4!=0 && (mode==3 || (mode==0 && width%16!=0)))continue;
                row(mode,width,offset,static_cast<BYTE>(alpha),color);
            }
    uint32_t random=0x95b071e3;
    const auto next=[&]() -> uint32_t {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<3000;++i) {
        const int mode=next()%4,width=next()%258;int offset=next()%16;
        if(mode==3 || (mode==0 && width%16!=0))offset&=~3;
        const BYTE alpha=static_cast<BYTE>(next());const DWORD color=next();row(mode,width,offset,alpha,color);
    }
    alias_control();
    for(int mode:{0,1,2,3})for(int width:{-1,0,1,2,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257})
        for(int offset=0;offset<16;++offset)
            for(int delta:{INT_MAX,-64,-33,-16,-1,0,1,3,4,7,15,16,31,32,63,64,255,511})
                for(unsigned salt:{0u,1u,63u,127u,128u,254u,255u}) {
                    if(width>0 && offset%4!=0 && (mode%2==0 || width%16!=0))continue;
                    masked(mode,width,offset,delta,0xc5793be1u^salt*0x01010101u,salt);
                }
    #if XY_PLANAR_REFERENCE
    for(bool simd:{false,true})for(int width:{-1,0,1,15,16,17,31,32,33,63,64,65,127,129,255,256,257})
        for(int offset=0;offset<16;++offset)
            for(int delta:{INT_MAX,-33,-16,-1,0,1,7,8,15,16,31,63,255})
                for(int src_stride:{-320,-8,0,8,320})for(int dst_stride:{-320,-8,0,8,320}) {
                    plane(simd,width,3,offset,delta,src_stride,dst_stride,173,71);
                }
    for(bool simd:{false,true})for(int height:{-1,0,1,3})
        for(int width:{-1,0,1,16,17,32,33,257})for(int offset=0;offset<16;++offset)
            for(unsigned alpha:{0u,1u,127u,128u,254u,255u})for(unsigned value:{0u,1u,127u,128u,254u,255u})
                plane(simd,width,height,offset,INT_MAX,320,320,static_cast<BYTE>(value),0,alpha);
    #endif
    for(bool planar:{false,true})for(int stride:{-1,0,1,2,-2})
        for(int width:{-1,0,1,15,16,31,32,33,63,129,257})for(int height:{-1,0,1,3})
            for(int x:{0,1,7,16})for(unsigned alpha:{0u,1u,127u,128u,254u,255u}) {
                const int pitch=(stride==2 || stride==-2) ? (stride>0 ? 8 : -8) : stride*(planar ? 320 : 1280);
                rectangle(planar,pitch,width,height,x,1,(alpha<<24)|0x79c2e5u);
            }
    printf("%llu exact native fill comparisons passed (%llu bytes compared including padding/guards, %llu visibly changed cases, %llu rectangles; all alpha/source/destination byte combinations, alignment boundaries, negative/zero pitch, overlapping rows/planes and grouped aliased-alpha reads)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytes_compared),
        static_cast<unsigned long long>(changed),static_cast<unsigned long long>(rectangles));
    printf("Masked planar comparisons: %llu; reference branch: %s\n",
        static_cast<unsigned long long>(planar_comparisons),
#if defined(_DEBUG)
        "original Debug"
#elif defined(_MSC_VER)
        "original MSVC Release"
#else
        "not exercised (use MSVC Release or original Debug)"
#endif
    );
#else
    printf("Native SSE fill comparison unavailable on this architecture; 0 native pixel comparisons performed\n");
#endif
}
