// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_pixels.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef std::array<RGBQUAD,256> Storage;
struct Outcome { Storage first,second; };
static uint64_t comparisons=0,changed=0,aliases=0;
static Storage initial(unsigned seed) {
    Storage result;
    for(size_t i=0;i<result.size();++i) {
        result[i].rgbBlue=static_cast<BYTE>(i*17+seed);
        result[i].rgbGreen=static_cast<BYTE>(i*31+(seed>>8));
        result[i].rgbRed=static_cast<BYTE>(i*7+(seed>>16));
        result[i].rgbReserved=static_cast<BYTE>(i*13+(seed>>24));
    }
    return result;
}
template<class Image> static Outcome execute(Storage result,CRect rect,CPoint point,int length,size_t color,unsigned palette,unsigned transparency,bool custom,int alias) {
    Image image;image.rect=rect;image.fCustomPal=custom;image.lpPixels=result.data()+48;
    image.orgpal=result.data()+(alias ? 48+alias : 8);image.cuspal=result.data()+(alias ? 48+alias : 28);
    for(unsigned i=0;i<4;++i){image.pal[i].pal=(palette+i*3)&15;image.pal[i].tr=(transparency+i*5)&15;}
    image.DrawPixels(point,length,color);
    Outcome outcome;outcome.first=result;
    // Repeated calls must read the palette after any earlier aliasing writes.
    image.DrawPixels(point,length,(color+1)&3);
    outcome.second=result;return outcome;
}
static void compare(CRect rect,CPoint point,int length,size_t color,unsigned palette,unsigned transparency,bool custom,int alias,unsigned seed) {
    const auto before=initial(seed);
    const auto a=execute<ReferenceVobPixels>(before,rect,point,length,color,palette,transparency,custom,alias);
    const auto b=execute<CandidateVobPixels>(before,rect,point,length,color,palette,transparency,custom,alias);
    if(std::memcmp(a.first.data(),b.first.data(),sizeof(Storage)) || std::memcmp(a.second.data(),b.second.data(),sizeof(Storage))) {
        fprintf(stderr,"VobSub pixel mismatch rect=%d,%d,%d,%d point=%d,%d len=%d color=%zu pal=%u tr=%u custom=%d alias=%d seed=%u\n",rect.left,rect.top,rect.right,rect.bottom,point.x,point.y,length,color,palette,transparency,custom,alias,seed);std::abort();
    }
    ++comparisons;changed+=std::memcmp(a.first.data(),before.data(),sizeof(Storage))!=0;aliases+=alias!=0;
}
template<class Image> static void rejectedWithoutPaletteRead() {
    Image image;image.rect=CRect(0,0,10,10);image.fCustomPal=false;
    image.orgpal=image.cuspal=image.lpPixels=nullptr;
    image.DrawPixels(CPoint(0,0),0,0);
    image.DrawPixels(CPoint(-20,0),3,0);
    image.DrawPixels(CPoint(20,0),3,0);
    image.DrawPixels(CPoint(0,-1),3,0);
    image.DrawPixels(CPoint(0,10),3,0);
}
int main() {
    for(int width:{1,2,3,7,12})for(int height:{1,2,5,8})for(int left:{-17,0,23})for(int top:{-9,0,31})
    for(int x:{-14,-2,-1,0,1,2,6,11,12,15})for(int y:{-1,0,1,4,7,8})for(int length:{-1,0,1,2,3,7,12,16})
    for(bool custom:{false,true})for(int alias:{0,1,7}) {
        const unsigned seed=static_cast<unsigned>(width*height+x*13+y*71+length*3);
        compare(CRect(left,top,left+width,top+height),CPoint(left+x,top+y),length,seed&3,seed&15,(seed>>4)&15,custom,alias,seed);
    }
    for(unsigned palette=0;palette<16;++palette)for(unsigned transparency=0;transparency<16;++transparency)
    for(unsigned byte=0;byte<256;++byte)for(bool custom:{false,true})for(int alias:{0,1,7}) {
        compare(CRect(0,0,12,4),CPoint(-1,0),10,byte&3,palette,transparency,custom,alias,byte*0x01010101U);
    }
    // Reversed dimensions and extreme coordinates which reject before unsafe
    // arithmetic are finite reference cases. Overflowing native expressions are
    // not executed or counted as parity evidence.
    for(int left:{-1,0,1})for(int right:{-1,0,1})for(int top:{-1,0,1})for(int bottom:{-1,0,1})
    for(int x:{-2,-1,0,1,2})for(int y:{-2,-1,0,1,2}) {
        // The backing array includes valid storage before lpPixels for the
        // small reversed-width cases; their final nonpositive runs do not write.
        compare(CRect(left,top,right,bottom),CPoint(x,y),1,0,0,0,false,0,0);
    }
    for(int x:{INT_MIN,INT_MAX})for(int length:{INT_MIN,-1,0})compare(CRect(0,0,1,1),CPoint(x,0),length,0,0,0,false,0,0);
    rejectedWithoutPaletteRead<ReferenceVobPixels>();rejectedWithoutPaletteRead<CandidateVobPixels>();
    // The first output overwrites palette entries: later output must still use
    // the single color snapshot, while the next call must use the new palette.
    auto before=initial(0x12345678);
    for(unsigned i=0;i<4;++i)before[28+i]=before[49+i];
    const auto alias=execute<ReferenceVobPixels>(before,CRect(0,0,12,4),CPoint(0,0),12,0,0,0,true,1);
    const auto separate=execute<ReferenceVobPixels>(before,CRect(0,0,12,4),CPoint(0,0),12,0,0,0,true,0);
    if(std::memcmp(alias.first.data()+48,separate.first.data()+48,12*sizeof(RGBQUAD))
       || !std::memcmp(alias.second.data()+48,separate.second.data()+48,12*sizeof(RGBQUAD))
       || !pixelFilled || !pixelFallback || !changed){fprintf(stderr,"VobSub pixel route/visibility control failed\n");return 1;}
    for(unsigned i=48;i<60;++i)if(std::memcmp(&alias.first[i],&before[49],sizeof(RGBQUAD))){fprintf(stderr,"VobSub palette snapshot control failed\n");return 1;}
    printf("%llu exact VobSub pixel comparisons passed (%llu bytes across both call checkpoints including palette/output guards, %llu visibly changed cases, %llu palette-alias cases, %llu Rust-handled clipping calls, %llu fallback calls, %llu Rust-filled spans; left-edge length retention, palette snapshots, repeated calls and all transparency/palette nibbles)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(comparisons*2*sizeof(Storage)),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(aliases),static_cast<unsigned long long>(pixelHandled),static_cast<unsigned long long>(pixelFallback),static_cast<unsigned long long>(pixelFilled));
}
