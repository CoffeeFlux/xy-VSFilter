// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_mask.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <climits>
static uint64_t comparisons=0,bytes_compared=0,changed=0;
static void apply(bool candidate,int mode,BYTE* dst,const BYTE* src,const BYTE* body,const BYTE* mask,int alpha,int w,int h,int pitch,int mpitch) {
    if(candidate) {
        if(mode==0)CandidateMask::FillAlphaMashBody_c(dst,src,alpha,w,h,pitch);
        if(mode==1)CandidateMask::FillAlphaMashBorder_c(dst,src,body,alpha,w,h,pitch);
        if(mode==2)CandidateMask::FillAlphaMashBodyMasked_c(dst,src,alpha,w,h,pitch,mask,mpitch);
        if(mode==3)CandidateMask::FillAlphaMashBorderMasked_c(dst,src,body,alpha,w,h,pitch,mask,mpitch);
    }else {
        if(mode==0)ReferenceMask::FillAlphaMashBody_c(dst,src,alpha,w,h,pitch);
        if(mode==1)ReferenceMask::FillAlphaMashBorder_c(dst,src,body,alpha,w,h,pitch);
        if(mode==2)ReferenceMask::FillAlphaMashBodyMasked_c(dst,src,alpha,w,h,pitch,mask,mpitch);
        if(mode==3)ReferenceMask::FillAlphaMashBorderMasked_c(dst,src,body,alpha,w,h,pitch,mask,mpitch);
    }
}
static void fixture(int mode,int alpha,int width,int height,int pitch,int mpitch,int offset,int source_delta,int body_delta,int mask_delta,int body_value=-1,int mask_value=-1,bool zero=false) {
    std::array<BYTE,4096> initial,source,body,mask,a,b;
    for(size_t i=0;i<initial.size();++i) {
        initial[i]=static_cast<BYTE>(i*37+(i>>8));
        source[i]=zero ? 0 : static_cast<BYTE>(i);
        body[i]=body_value<0 ? static_cast<BYTE>(i*71+(i>>8)) : static_cast<BYTE>(body_value);
        mask[i]=mask_value<0 ? static_cast<BYTE>(i*53+(i>>8)) : static_cast<BYTE>(mask_value);
    }
    a=initial;b=initial;
    const int start=1536+offset;
    BYTE* pa=a.data()+start;BYTE* pb=b.data()+start;
    apply(false,mode,pa,source_delta==INT_MAX ? source.data()+start : pa+source_delta,
        body_delta==INT_MAX ? body.data()+start : pa+body_delta,
        mask_delta==INT_MAX ? mask.data()+start : pa+mask_delta,alpha,width,height,pitch,mpitch);
    apply(true,mode,pb,source_delta==INT_MAX ? source.data()+start : pb+source_delta,
        body_delta==INT_MAX ? body.data()+start : pb+body_delta,
        mask_delta==INT_MAX ? mask.data()+start : pb+mask_delta,alpha,width,height,pitch,mpitch);
    if(a!=b) {
        fprintf(stderr,"Mask mismatch mode=%d alpha=%d width=%d height=%d pitch=%d mpitch=%d aliases=%d,%d,%d\n",mode,alpha,width,height,pitch,mpitch,source_delta,body_delta,mask_delta);
        exit(1);
    }
    for(size_t i=0;i<64;++i)if(a[i]!=initial[i] || a[a.size()-1-i]!=initial[a.size()-1-i]) {
        fprintf(stderr,"Mask fixture changed outer guard\n");exit(1);
    }
    ++comparisons;bytes_compared+=a.size();if(a!=initial)++changed;
}
int main() {
    for(int alpha=0;alpha<256;++alpha) {
        fixture(0,alpha,256,1,0,0,0,INT_MAX,INT_MAX,INT_MAX);
        for(int value=0;value<256;++value) {
            fixture(1,alpha,256,1,0,0,0,INT_MAX,INT_MAX,INT_MAX,value);
            fixture(2,alpha,256,1,0,0,0,INT_MAX,INT_MAX,INT_MAX,0,value);
            fixture(3,alpha,256,1,0,0,0,INT_MAX,INT_MAX,INT_MAX,value,alpha^value);
        }
    }
    for(int mode:{0,1,2,3})for(int width:{-1,0,1,3,4,15,16,17,31,32,33,255,256,257})for(int height:{0,1,3})
        for(int pitch:{-320,-8,0,8,320})for(int mpitch:{-320,0,320})
            for(int delta:{INT_MAX,-33,-16,-1,0,1,7,16,255}) {
                fixture(mode,173,width,height,pitch,mpitch,7,delta,delta,delta);
                fixture(mode,-173,width,height,pitch,mpitch,3,INT_MAX,delta,INT_MAX);
                fixture(mode,257,width,height,pitch,mpitch,0,delta,INT_MAX,-1);
            }
    for(int mode:{0,1,2,3})for(int alpha:{-33025,-32768,-257,-1,0,1,63,64,65,127,128,255,256,257,32767,33025})
        for(int value:{0,1,63,64,65,127,128,254,255})
            fixture(mode,alpha,256,3,-320,320,15,INT_MAX,INT_MAX,INT_MAX,value,255-value);
    for(int mode:{0,1})for(int alpha:{-8421504,-8421503,8421503,8421504})
        fixture(mode,alpha,256,1,0,0,0,INT_MAX,INT_MAX,INT_MAX,0,255);
    for(int mode:{0,1,2,3})for(int alpha:{INT_MIN,INT_MAX})
        fixture(mode,alpha,256,3,320,-320,0,INT_MAX,INT_MAX,INT_MAX,255,255,true);
    uint32_t random=0x371957efU;
    const auto next=[&]() -> uint32_t {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<3000;++i) {
        const int mode=next()%4,alpha=static_cast<int>(next()%66051)-33025,width=next()%258;
        const int body=next()%256,mask=next()%256,offset=next()%16;
        fixture(mode,alpha,width,3,320,-320,offset,INT_MAX,INT_MAX,INT_MAX,body,mask);
    }
    printf("%llu exact scalar mask comparisons passed (%llu bytes including padding/guards, %llu visibly changed cases; body/border, masked/unmasked, signed opacity, wrapping byte output, source/destination/mask overlap, negative/zero pitches)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytes_compared),static_cast<unsigned long long>(changed));
}
