// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_hdmv.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

struct Outcome {
    std::vector<HdmvEvent> trace;
    std::vector<BYTE> pixels;
    std::array<DWORD,256> palette;
    int exception=0;
    bool operator==(const Outcome& b) const {
        return trace==b.trace && pixels==b.pixels && palette==b.palette && exception==b.exception;
    }
};
template<class Owner> Outcome run(std::vector<BYTE> data,short x,short y,short height,
    int failure,bool palette,bool null_data,int pixel_type,bool change_palette) {
    // Non-null zero-length input is distinct from an absent buffer.
    const int size=static_cast<int>(data.size()); data.push_back(0x7b);
    Owner owner; owner.m_pRLEData=null_data ? nullptr : data.data(); owner.m_nRLEDataSize=size;
    owner.m_horizontal_position=x; owner.m_vertical_position=y; owner.m_height=height;owner.palette=palette;
    for(unsigned i=0;i<256;++i)owner.m_Colors[i]=i*0x013b97e5u ^ 0x81050703u;
    Outcome outcome;
    SubPicDesc spd;
    if(pixel_type>=0) {
        // Both packed and four-plane surfaces total this many bytes. Include
        // row padding and outer guards in the exact comparison.
        outcome.pixels.resize(16+4*272*4+16);
        for(size_t i=0;i<outcome.pixels.size();++i)outcome.pixels[i]=static_cast<BYTE>(i*37+11);
        spd.type=pixel_type;spd.pitch=pixel_type==MSP_AYUV_PLANAR ? 272 : 272*4;spd.h=4;
        spd.bits=outcome.pixels.data()+16;
    }
    hdmvPixels=pixel_type>=0;hdmvChangePalette=change_palette;
    hdmvObserve=[&](HdmvEvent event){
        outcome.trace.push_back(event);
        if(static_cast<int>(outcome.trace.size())==failure)throw 17;
    };
    try { owner.RenderHdmv(spd); } catch(int error) { outcome.exception=error; }
    std::copy(owner.m_Colors,owner.m_Colors+256,outcome.palette.begin());
    hdmvObserve=nullptr;
    return outcome;
}
static uint64_t comparisons=0,events=0,exceptions=0,pixel_comparisons=0,visible_pixel_cases=0;
static void check(const std::vector<BYTE>& data,short x=0,short y=0,short height=3,
    bool inject=false,bool palette=true,bool null_data=false,int pixel_type=-1,bool change_palette=false) {
    const auto normal=run<OriginalHdmv>(data,x,y,height,0,palette,null_data,pixel_type,change_palette);
    if(normal.exception) { fprintf(stderr,"Invalid HDMV reference test setup: %d\n",normal.exception);exit(1); }
    if(pixel_type>=0) {
        bool changed=false;
        for(size_t i=0;i<normal.pixels.size();++i) {
            const bool different=normal.pixels[i]!=static_cast<BYTE>(i*37+11);
            if(different && (i<16 || i>=normal.pixels.size()-16)) {
                fprintf(stderr,"HDMV pixel guard changed\n");exit(1);
            }
            changed|=different;
        }
        if(!changed) { fprintf(stderr,"HDMV pixel fixture did not draw\n");exit(1); }
        ++visible_pixel_cases;
    }
    for(int failure=0,last=inject ? static_cast<int>(normal.trace.size()) : 0;failure<=last;++failure) {
        const auto a=failure ? run<OriginalHdmv>(data,x,y,height,failure,palette,null_data,pixel_type,change_palette) : normal;
        const auto b=run<CandidateHdmv>(data,x,y,height,failure,palette,null_data,pixel_type,change_palette);
        if(!(a==b)) {
            fprintf(stderr,"HDMV mismatch %llu at %d,%d height %d failure %d bytes",static_cast<unsigned long long>(comparisons),x,y,height,failure);
            for(auto byte:data)fprintf(stderr," %02x",byte);
            fprintf(stderr,"\n");exit(1);
        }
        ++comparisons;events+=a.trace.size();if(failure)++exceptions;
        if(pixel_type>=0)++pixel_comparisons;
    }
}
int main() {
    check({});
    for(unsigned a=0;a<256;++a) {
        check({static_cast<BYTE>(a)});
        for(unsigned b=0;b<256;++b) {
            check({static_cast<BYTE>(a),static_cast<BYTE>(b)});
            check({0,static_cast<BYTE>(a),static_cast<BYTE>(b)});
            check({0,static_cast<BYTE>(a),static_cast<BYTE>(b),255,1,0,0,7});
        }
    }
    // Every length/palette combination for selected extended and explicit forms,
    // including each truncation point and subsequent state transitions.
    for(unsigned mode: {0x40u,0x80u,0xc0u,0xffu})for(unsigned length=0;length<256;++length)
        for(unsigned palette=0;palette<256;++palette)
            check({0,static_cast<BYTE>(mode),static_cast<BYTE>(length),static_cast<BYTE>(palette),1,0,0,3});
    const short edges[]={SHRT_MIN,SHRT_MIN+1,-1,0,1,SHRT_MAX-1,SHRT_MAX};
    for(auto x:edges)for(auto y:edges)for(auto height:edges) {
        check({0,0xff,255,4,255,9,0,0,7,0,0,0,0xc0,1,2},x,y,height,true);
        check({0,0,0,0,0,0,3},x,y,height);
    }
    for(bool palette:{false,true})for(bool null_data:{false,true})
        check({1,0,0xc1},0,0,3,true,palette,null_data);
    uint32_t random=0xb75ce911;
    for(unsigned trial=0;trial<20000;++trial) {
        std::vector<BYTE> data;
        for(unsigned i=0;i<trial%128;++i) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            data.push_back((random&3)==0 ? 0 : static_cast<BYTE>(random>>8));
        }
        check(data,static_cast<short>(random),static_cast<short>(random>>16),static_cast<short>(trial%5),trial%1000==0,true,false,-1,true);
    }
#if HDMV_NATIVE_PIXELS
    for(int type:{0,static_cast<int>(MSP_AYUV_PLANAR)})for(int x:{0,1,7,15,16})for(int width:{1,15,16,31,32,33,63,64,127,191})
        for(unsigned palette:{0u,1u,7u,127u,254u,255u}) {
            const std::vector<BYTE> data={0,static_cast<BYTE>(0xc0|(width>>8)),static_cast<BYTE>(width),static_cast<BYTE>(palette),0,0,3,7,0,0,0,0xc0,33,4};
            check(data,static_cast<short>(x),0,3,true,true,false,type,true);
        }
#endif
    printf("%llu exact HDMV comparisons passed (%llu EOF/byte/draw observations, %llu injected boundary exceptions, %llu native pixel comparisons, %llu visible pixel cases)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(events),
        static_cast<unsigned long long>(exceptions),static_cast<unsigned long long>(pixel_comparisons),
        static_cast<unsigned long long>(visible_pixel_cases));
}
