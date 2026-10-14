// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_gauss_integer.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
typedef std::array<unsigned,16384> Storage;
static uint64_t comparisons=0,bytesCompared=0,changed=0,imageChanged=0,aliasCases=0;
static std::vector<unsigned> table(int radius,int pattern,bool vertical) {
    const int width=2*radius+1;const unsigned volume=1U<<22;
    std::vector<unsigned> weights(width,0),result(width*256,0);
    if(pattern==0)weights[radius]=volume;
    else if(pattern!=4 && pattern!=5) {
        unsigned total=0;
        for(int i=0;i<width;++i) {weights[i]=pattern==2 ? radius+1-std::abs(i-radius) : pattern==3 ? i+1 : 1;total+=weights[i];}
        unsigned sum=0;for(auto& weight:weights){weight*=volume/total;sum+=weight;}
        weights[radius]+=volume-sum;
    }
    for(unsigned value=0;value<256;++value)for(int i=0;i<width;++i) {
        result[value*width+i]=pattern==5 ? (value*width+i)%123 : pattern==6 && vertical ? 0xfffffff0U+static_cast<unsigned>(i) : value*weights[i];
    }
    return result;
}
static void compare(const Storage& seed,int width,int height,int stride,int rx,int ry,const std::vector<unsigned>& tx,const std::vector<unsigned>& ty,int delta,int passes) {
    auto a=seed,b=seed;
    const int imageOffset=delta==100000 ? 8192 : 4096*4+delta;
    for(int pass=0;pass<passes;++pass) {
        ReferenceIntegerGauss::ass_gauss_blur(reinterpret_cast<unsigned char*>(a.data())+imageOffset,a.data()+4096,width,height,stride,tx.data(),rx,2*rx+1,ty.data(),ry,2*ry+1);
        CandidateIntegerGauss::ass_gauss_blur(reinterpret_cast<unsigned char*>(b.data())+imageOffset,b.data()+4096,width,height,stride,tx.data(),rx,2*rx+1,ty.data(),ry,2*ry+1);
    }
    if(a!=b) {
        fprintf(stderr,"Integer Gaussian mismatch width=%d height=%d stride=%d rx=%d ry=%d aliasDelta=%d passes=%d\n",width,height,stride,rx,ry,delta,passes);std::abort();
    }
    for(size_t i=0;i<16;++i)if(a[i]!=seed[i] || a[a.size()-1-i]!=seed[a.size()-1-i])std::abort();
    bool visible=false;
    const auto pixels=reinterpret_cast<const unsigned char*>(a.data())+imageOffset;
    const auto original=reinterpret_cast<const unsigned char*>(seed.data())+imageOffset;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)visible|=pixels[y*stride+x]!=original[y*stride+x];
    ++comparisons;bytesCompared+=sizeof(a);changed+=a!=seed;imageChanged+=visible;aliasCases+=delta!=100000;
}
int main() {
    Storage seed;
    for(unsigned pattern=0;pattern<4;++pattern) {
        unsigned char* bytes=reinterpret_cast<unsigned char*>(seed.data());
        for(size_t i=0;i<sizeof(seed);++i)bytes[i]=pattern==0 ? 0 : pattern==1 ? 255 : static_cast<unsigned char>(i*(pattern*71)+i/17);
        for(int rx=0;rx<=3;++rx)for(int ry=0;ry<=3;++ry)for(int coefficientPattern=0;coefficientPattern<7;++coefficientPattern) {
            const auto tx=table(rx,coefficientPattern,false),ty=table(ry,coefficientPattern,true);
            const int minimumHeight=std::max(2*ry+1,3*ry);
            for(int width:{2*rx+1,2*rx+2,2*rx+8,33})for(int height:{minimumHeight,minimumHeight+1,minimumHeight+3,33})
                for(int stride:{-64,-1,0,1,33,64})for(int passes:{1,2})
                    compare(seed,width,height,stride,rx,ry,tx,ty,100000,passes);
            // Keep alias coefficients normalized so the original signed lookup
            // setup remains bounded while source bytes change during accumulation.
            if(coefficientPattern<4)for(int delta:{-65,-17,-1,0,1,17,65})for(int stride:{-64,0,1,64})
                compare(seed,17,17,stride,rx,ry,tx,ty,delta,2);
        }
    }
    {
        unsigned char pixels[]={1,2,3,4};unsigned scratch[6]={};const auto identity=table(0,0,false);
        ReferenceIntegerGauss::ass_gauss_blur(pixels,scratch,2,2,2,identity.data(),0,1,identity.data(),0,1);
        const unsigned char expected[]={0,0,0,4};
        if(std::memcmp(pixels,expected,sizeof(pixels))){fprintf(stderr,"Radius-zero reference control failed\n");return 1;}
    }
    if(!integerGaussHandled || !integerGaussFallback){fprintf(stderr,"Integer Gaussian route controls failed\n");return 1;}
    printf("%llu exact integer-Gaussian comparisons passed (%llu bytes including image/scratch padding and guards, %llu changed storage cases, %llu visibly changed images, %llu image/scratch alias cases, %llu Rust-handled calls, %llu native fallback calls; asymmetric tables, cumulative edges, radius zero, unsigned wrapping and repeated overlapping rows)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(bytesCompared),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(imageChanged),static_cast<unsigned long long>(aliasCases),static_cast<unsigned long long>(integerGaussHandled),static_cast<unsigned long long>(integerGaussFallback));
}
