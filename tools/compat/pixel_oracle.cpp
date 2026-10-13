// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_hdmv.h"
#include <cstdio>
#include <cstdlib>
#if HDMV_NATIVE_PIXELS
static uint64_t scalar_count=0,sse_count=0,scaled_count=0,excluded_scalar=0;
static void same(DWORD a,DWORD b,const char* kind,DWORD dst,DWORD color,DWORD alpha,DWORD clip=0) {
    if(a!=b) {
        fprintf(stderr,"Pixel mismatch %s dst=%08x color=%08x alpha=%08x clip=%08x reference=%08x candidate=%08x\n",kind,dst,color,alpha,clip,a,b);
        exit(1);
    }
}
static void pixel(DWORD dst,DWORD color,DWORD alpha) {
    DWORD a=dst,b=dst;
    ReferencePixels::pixmix_sse2(&a,color,alpha);
    CandidatePixels::pixmix_sse2(&b,color,alpha);
    same(a,b,"SSE",dst,color,alpha);++sse_count;
    // Do not admit native signed-setup overflow as successful parity. The
    // production scalar adapter retains the original body for these states.
    if(alpha<0x7fffffffU || alpha>0x80000100U) {
        a=dst;b=dst;
        ReferencePixels::pixmix_c(&a,color,alpha);
        CandidatePixels::pixmix_c(&b,color,alpha);
        same(a,b,"scalar",dst,color,alpha);++scalar_count;
    }else ++excluded_scalar;
}
static void scaled(DWORD dst,DWORD color,DWORD shape,DWORD clip) {
    DWORD a=dst,b=dst;
    ReferencePixels::pixmix2(&a,color,shape,clip);
    CandidatePixels::pixmix2(&b,color,shape,clip);
    same(a,b,"scaled scalar",dst,color,shape,clip);++scaled_count;
    a=dst;b=dst;
    ReferencePixels::pixmix2_sse2(&a,color,shape,clip);
    CandidatePixels::pixmix2_sse2(&b,color,shape,clip);
    same(a,b,"scaled SSE",dst,color,shape,clip);++scaled_count;
}
#endif
int main() {
#if HDMV_NATIVE_PIXELS
    for(DWORD alpha=0;alpha<256;++alpha)for(DWORD value=0;value<256;++value)for(DWORD dest=0;dest<256;++dest) {
        const DWORD dst=dest|(((dest+83)&255)<<8)|(((dest+171)&255)<<16)|(((dest+32)&255)<<24);
        pixel(dst,((value^alpha)<<24)|(value*0x010101U),alpha);
    }
    for(DWORD high:{0U,1U,0x7fffU,0x8000U,0xffffU})for(DWORD low=0;low<65536;++low) {
        const DWORD alpha=(high<<16)|low;
        pixel(alpha*0x9127313U,alpha^0xc3579be1U,alpha);
    }
    for(DWORD alpha:{0U,1U,255U,256U,257U,32767U,32768U,65535U,65536U,0x7ffffffeU,0x7fffffffU,0x80000000U,0x80000100U,0x80000101U,0xffffffffU})
        for(DWORD color:{0U,1U,0xffffffffU,0xff000000U,0x00ffffffU,0x793be1c5U})
            for(DWORD dst:{0U,1U,0xffffffffU,0xff000000U,0x00ffffffU,0x793be1c5U})pixel(dst,color,alpha);
    for(DWORD shape=0;shape<256;++shape)for(DWORD clip=0;clip<256;++clip)for(DWORD alpha=0;alpha<256;++alpha) {
        const DWORD color=(alpha<<24)|(((shape*37+clip*71+alpha*11)*0x010307U)&0xffffffU);
        scaled(color^0x91c573e7U,color,shape,clip);
    }
    uint32_t random=0x85971becU;
    const auto next=[&]() -> uint32_t {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<1000000;++i) {
        const DWORD dst=next(),color=next(),alpha=next(),clip=next();
        pixel(dst,color,alpha);scaled(dst,color,alpha,clip);
    }
    DWORD a=0,b=0;
    ReferencePixels::pixmix_c(&a,0xffffff,257);
    ReferencePixels::pixmix_sse2(&b,0xffffff,257);
    if(a==b){fprintf(stderr,"Wider-alpha arithmetic control failed\n");return 1;}
    printf("%llu exact single-pixel comparisons passed (%llu scalar, %llu SSE, %llu scaled; %llu scalar setup-overflow cases excluded and retained native; all byte channel/alpha combinations, signed SSE weights, saturation, wrapping scaled products)\n",
        static_cast<unsigned long long>(scalar_count+sse_count+scaled_count),static_cast<unsigned long long>(scalar_count),
        static_cast<unsigned long long>(sse_count),static_cast<unsigned long long>(scaled_count),static_cast<unsigned long long>(excluded_scalar));
#else
    printf("Native SSE single-pixel comparison unavailable on this architecture; 0 comparisons performed\n");
#endif
}
