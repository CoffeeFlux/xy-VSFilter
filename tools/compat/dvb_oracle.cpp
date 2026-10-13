// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_dvb.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

struct Outcome {
    std::vector<std::vector<int64_t>> trace;
    std::vector<int64_t> state;
    std::vector<BYTE> pixels;
    int exception=0;
    bool operator==(const Outcome& b) const { return trace==b.trace && state==b.state && pixels==b.pixels && exception==b.exception; }
};
template<class Owner> Outcome run(int mode,std::vector<BYTE> data,short x,short y,short width,
    int seed,int failure,int flags,int pixel_type) {
    const int size=static_cast<int>(data.size());data.push_back(0x5d);
    ObservedDvbBuffer buffer(data.data(),size);
    // Seed the exact native bit-buffer state, including peeked whole bytes.
    if(seed>0)buffer.ReferenceGolomb::BitRead(seed);
    if(seed<0)buffer.ReferenceGolomb::BitRead(-seed,true);
    Owner owner;owner.m_width=width;
    for(unsigned i=0;i<256;++i)owner.m_Colors[i]=i*0x013b97e5u ^ 0x81050703u;
    RECT dirty=(flags&8) ? RECT{9,10,-1,-2} : RECT{-1,1,2,3};
    RECT* dirty_ptr=(flags&1) ? &dirty : nullptr;
    short& y_ref=(flags&2) ? x : y;
    Outcome outcome;
    const auto snapshot=[&]() {
        std::vector<int64_t> state={x,y,y_ref,dirty.left,dirty.top,dirty.right,dirty.bottom,
            buffer.m_nBitPos,buffer.m_bitlen,buffer.m_bitbuff};
        for(auto c:owner.m_Colors)state.push_back(c);
        return state;
    };
    SubPicDesc spd;
    if(pixel_type>=0) {
        outcome.pixels.resize(16+4*304*4+16);
        for(size_t i=0;i<outcome.pixels.size();++i)outcome.pixels[i]=static_cast<BYTE>(i*37+11);
        spd.type=pixel_type;spd.pitch=pixel_type==MSP_AYUV_PLANAR ? 304 : 304*4;spd.h=4;
        spd.bits=outcome.pixels.data()+16;
    }
    hdmvPixels=pixel_type>=0;hdmvChangePalette=(flags&4)!=0;
    hdmvObserve=[&](HdmvEvent event) {
        auto state=snapshot();state.insert(state.begin(),event.begin(),event.end());outcome.trace.push_back(state);
        if(static_cast<int>(outcome.trace.size())==failure)throw 17;
    };
    try {
        owner.Run(mode,spd,buffer,x,y_ref,dirty_ptr);
        // Reusing the aligned reader and coordinate references is observable.
        if(flags&16)owner.Run(mode,spd,buffer,x,y_ref,dirty_ptr);
    } catch(int error) { outcome.exception=error; }
    outcome.state=snapshot();hdmvObserve=nullptr;return outcome;
}
static uint64_t comparisons=0,observations=0,exceptions=0,pixels=0,visible=0;
static void check(int mode,const std::vector<BYTE>& data,short x=0,short y=0,short width=100,
    int seed=0,int flags=1,bool inject=false,int pixel_type=-1) {
    const auto normal=run<OriginalDvb>(mode,data,x,y,width,seed,0,flags,pixel_type);
    if(normal.exception){fprintf(stderr,"Invalid DVB reference fixture %d\n",normal.exception);exit(1);}
    if(pixel_type>=0) {
        bool changed=false;
        for(size_t i=0;i<normal.pixels.size();++i) {
            const bool different=normal.pixels[i]!=static_cast<BYTE>(i*37+11);
            if(different && (i<16 || i>=normal.pixels.size()-16)){fprintf(stderr,"DVB pixel guard changed\n");exit(1);}
            changed|=different;
        }
        if(!changed){fprintf(stderr,"DVB pixel fixture did not draw\n");exit(1);}++visible;
    }
    for(int failure=0,last=inject ? static_cast<int>(normal.trace.size()) : 0;failure<=last;++failure) {
        const auto a=failure ? run<OriginalDvb>(mode,data,x,y,width,seed,failure,flags,pixel_type) : normal;
        const auto b=run<CandidateDvb>(mode,data,x,y,width,seed,failure,flags,pixel_type);
        if(!(a==b)) {
            fprintf(stderr,"DVB mismatch %llu mode %d seed %d flags %d x/y/width %d/%d/%d failure %d bytes",static_cast<unsigned long long>(comparisons),mode,seed,flags,x,y,width,failure);
            for(auto byte:data)fprintf(stderr," %02x",byte);fprintf(stderr,"\n");exit(1);
        }
        ++comparisons;observations+=a.trace.size();if(failure)++exceptions;if(pixel_type>=0)++pixels;
    }
}
static std::vector<BYTE> encoded(int mode,int selector,unsigned length,unsigned palette) {
    std::vector<BYTE> data;unsigned position=0;
    const auto bits=[&](unsigned value,unsigned count){
        for(unsigned bit=count;bit>0;--bit){if(position%8==0)data.push_back(0);data.back()|=((value>>(bit-1))&1)<<(7-position%8);++position;}
    };
    bits(0,mode);
    if(mode==8){bits(1,1);bits(length,7);bits(palette,8);}
    else if(selector==0){
        if(mode==2){bits(1,1);bits(length,3);bits(palette,2);}
        else {bits(1,1);bits(0,1);bits(length,2);bits(palette,4);}
    }else {
        bits(mode==2 ? 0 : 1,1);bits(mode==2 ? 0 : 1,1);
        bits(selector==1 ? 2 : 3,2);bits(length,selector==1 ? 4 : 8);bits(palette,mode);
    }
    bits(0,16);return data;
}
int main() {
    for(int mode:{2,4,8}) {
        check(mode,{});
        for(unsigned a=0;a<256;++a) {
            check(mode,{static_cast<BYTE>(a)});
            for(unsigned b=0;b<256;++b)check(mode,{static_cast<BYTE>(a),static_cast<BYTE>(b)});
            for(int seed:{1,2,3,4,5,6,7,-1,-8,-16})
                check(mode,{static_cast<BYTE>(a),0xe5,0,0,0x79},-2,1,23,seed,21);
        }
        for(int selector=0;selector<(mode==8 ? 1 : 3);++selector) {
            const unsigned lengths=mode==8 ? 128 : selector==2 ? 256 : selector==1 ? 16 : mode==2 ? 8 : 4;
            for(unsigned length=0;length<lengths;++length)for(unsigned palette=0;palette<(1u<<mode);++palette)
                check(mode,encoded(mode,selector,length,palette),-1,2,32767);
        }
        const short edge[]={SHRT_MIN,SHRT_MIN+1,-1,0,1,SHRT_MAX-1,SHRT_MAX};
        for(short x:edge)for(short y:edge)for(short width:edge)for(int flags:{0,1,3,5,9,17})
            check(mode,{0x7f,0x81,0x10,0xff,0,0,0xff,0xff},x,y,width,0,flags,true);
    }
    uint32_t random=0xd7b451a3;
    for(unsigned trial=0;trial<12000;++trial) {
        std::vector<BYTE> data;
        for(unsigned i=0;i<trial%48;++i){random^=random<<13;random^=random>>17;random^=random<<5;data.push_back((random&3)==0 ? 0 : static_cast<BYTE>(random>>8));}
        check(2<<(trial%3),data,static_cast<short>(random),static_cast<short>(random>>16),static_cast<short>(random>>8),0,trial%32,trial%1000==0);
    }
#if HDMV_NATIVE_PIXELS
    for(int mode:{2,4,8})for(int type:{0,static_cast<int>(MSP_AYUV_PLANAR)})for(int x:{0,1,15,16})
        for(unsigned length:{0u,1u,15u,16u,31u,32u,33u,63u,127u})for(unsigned palette:{0u,1u,2u,3u,7u,15u,127u,255u}) {
            if(mode==8 && length==0)continue;
            check(mode,encoded(mode,2,length,palette&((1u<<mode)-1)),static_cast<short>(x),1,300,0,5,true,type);
        }
#endif
    printf("%llu exact DVB comparisons passed (%llu native observations, %llu injected boundary exceptions, %llu native pixel comparisons, %llu visible pixel cases)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(observations),static_cast<unsigned long long>(exceptions),
        static_cast<unsigned long long>(pixels),static_cast<unsigned long long>(visible));
}
