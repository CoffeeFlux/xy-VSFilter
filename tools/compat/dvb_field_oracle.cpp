// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_dvb.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

struct FieldOutcome {
    std::vector<std::vector<int64_t>> trace;
    std::vector<int64_t> state;
    std::vector<BYTE> pixels;
    int exception=0;
    bool operator==(const FieldOutcome& b) const {
        return trace==b.trace && state==b.state && pixels==b.pixels && exception==b.exception;
    }
};
template<class Owner> FieldOutcome run(const std::vector<BYTE>& input,short length,short x,short y,
    short width,int seed,int flags,int failure,int pixel_type) {
    std::vector<BYTE> data=input;const int size=static_cast<int>(data.size());data.push_back(0x5d);
    ObservedDvbBuffer buffer(data.data(),size);
    if(seed>0)buffer.ReferenceGolomb::BitRead(seed);
    if(seed<0)buffer.ReferenceGolomb::BitRead(-seed,true);
    Owner owner;owner.m_width=width;
    owner.m_pRLEData=(flags&32) ? nullptr : data.data();owner.m_nRLEDataSize=size;
    for(unsigned i=0;i<256;++i)owner.m_Colors[i]=i*0x013b97e5u ^ 0x81050703u;
    RECT dirty=(flags&8) ? RECT{9,10,-1,-2} : RECT{-1,1,2,3};
    RECT* dirty_ptr=(flags&1) ? &dirty : nullptr;
    FieldOutcome outcome;
    const auto snapshot=[&]() {
        std::vector<int64_t> state={x,y,dirty.left,dirty.top,dirty.right,dirty.bottom,
            buffer.m_nBitPos,buffer.m_bitlen,buffer.m_bitbuff};
        for(auto c:owner.m_Colors)state.push_back(c);
        return state;
    };
    SubPicDesc spd;
    if(pixel_type>=0) {
        outcome.pixels.resize(16+16*304*4+16);
        for(size_t i=0;i<outcome.pixels.size();++i)outcome.pixels[i]=static_cast<BYTE>(i*37+11);
        spd.type=pixel_type;spd.pitch=pixel_type==MSP_AYUV_PLANAR ? 304 : 304*4;spd.h=16;
        spd.bits=outcome.pixels.data()+16;
    }
    hdmvPixels=pixel_type>=0;hdmvChangePalette=(flags&4)!=0;
    hdmvObserve=[&](HdmvEvent event) {
        auto state=snapshot();state.insert(state.begin(),event.begin(),event.end());outcome.trace.push_back(state);
        // A bounded oracle must never silently count a nonterminating field.
        if(outcome.trace.size()>20000)throw 23;
        if(static_cast<int>(outcome.trace.size())==failure)throw 17;
    };
    try {
        if(flags&2)owner.RenderDvb(spd,x,y,dirty_ptr);
        else owner.DvbRenderField(spd,buffer,x,y,length,dirty_ptr);
        if(flags&16) {
            if(flags&2)owner.RenderDvb(spd,x,y,dirty_ptr);
            else owner.DvbRenderField(spd,buffer,x,y,0,dirty_ptr);
        }
    }catch(int error){outcome.exception=error;}
    outcome.state=snapshot();hdmvObserve=nullptr;return outcome;
}
static uint64_t comparisons=0,observations=0,exceptions=0,pixels=0,visible=0;
static void check(const std::vector<BYTE>& data,short length,short x=0,short y=0,short width=100,
    int seed=0,int flags=1,bool inject=false,int pixel_type=-1) {
    const auto normal=run<OriginalDvb>(data,length,x,y,width,seed,flags,0,pixel_type);
    if(normal.exception){fprintf(stderr,"Unresolved DVB field reference fixture %d\n",normal.exception);exit(1);}
    if(pixel_type>=0) {
        bool changed=false;
        for(size_t i=0;i<normal.pixels.size();++i) {
            const bool different=normal.pixels[i]!=static_cast<BYTE>(i*37+11);
            if(different && (i<16 || i>=normal.pixels.size()-16)){fprintf(stderr,"DVB field pixel guard changed\n");exit(1);}
            changed|=different;
        }
        if(!changed){fprintf(stderr,"DVB field pixel fixture did not draw\n");exit(1);}++visible;
    }
    for(int failure=0,last=inject ? static_cast<int>(normal.trace.size()) : 0;failure<=last;++failure) {
        const auto a=failure ? run<OriginalDvb>(data,length,x,y,width,seed,flags,failure,pixel_type) : normal;
        const auto b=run<CandidateDvb>(data,length,x,y,width,seed,flags,failure,pixel_type);
        if(!(a==b)) {
            fprintf(stderr,"DVB field mismatch %llu length %d seed %d flags %d x/y/width %d/%d/%d failure %d bytes",
                static_cast<unsigned long long>(comparisons),length,seed,flags,x,y,width,failure);
            for(auto byte:data)fprintf(stderr," %02x",byte);fprintf(stderr,"\n");exit(1);
        }
        ++comparisons;observations+=a.trace.size();if(failure)++exceptions;if(pixel_type>=0)++pixels;
    }
}
static std::vector<BYTE> packet(const std::vector<BYTE>& top,const std::vector<BYTE>& bottom) {
    std::vector<BYTE> data={static_cast<BYTE>(top.size()>>8),static_cast<BYTE>(top.size()),
        static_cast<BYTE>(bottom.size()>>8),static_cast<BYTE>(bottom.size())};
    data.insert(data.end(),top.begin(),top.end());data.insert(data.end(),bottom.begin(),bottom.end());return data;
}
int main() {
    for(short length:std::vector<short>{SHRT_MIN,-1,0})check({},length);
    for(unsigned a=0;a<256;++a) {
        check({static_cast<BYTE>(a)},1);
        for(unsigned b=0;b<256;++b)check({static_cast<BYTE>(a),static_cast<BYTE>(b)},2);
        for(int seed:{1,2,3,4,5,6,7,-1,-8,-16})
            for(short length:{static_cast<short>(-1),static_cast<short>(0),static_cast<short>(1),static_cast<short>(4)})
                check({static_cast<BYTE>(a),0xf0,0x12,1,0,0,0x20,0x10,0x11},length,-2,1,23,seed,21);
    }
    const std::vector<BYTE> mixed={0x10,0x40,0,0x11,0x12,0,0x12,1,2,0,0,0xf0,0x12,3,0,0,
        0x20,0x10,0xf0,0x21,0x12,1,0,0,0x22,0x10,0x11,0x12,0xf0,0,0,0,0,0,0,0,0,0,0,0,0,0xf0};
    const short edge[]={SHRT_MIN,SHRT_MIN+1,-1,0,1,SHRT_MAX-1,SHRT_MAX};
    for(short x:edge)for(short y:edge)for(short width:edge)for(int flags:{0,1,5,9,17})
        check(mixed,static_cast<short>(mixed.size()),x,y,width,0,flags,true);
    // The top and bottom fields share one cursor; negative lengths consume no
    // field bytes, and the bottom Y is the native narrowing of start Y + 1.
    for(short x:edge)for(short y:edge)for(int flags:{2,3,7,11,19,35}) {
        check(packet(mixed,mixed),0,x,y,100,0,flags,true);
        check({0xff,0xff,0,3,0x12,1,0},0,x,y,100,0,flags,true);
        check({0,1,0xff,0xff,0x22},0,x,y,100,0,flags,true);
        check({},0,x,y,100,0,flags,true);
    }
    // Field lengths can end inside a pixel string; its decoder still consumes
    // its own termination before the next field starts.
    check({0,1,0,3,0x12,1,0,0,0x12,2,0},0,0,0,100,0,7,true);
    check(std::vector<BYTE>(SHRT_MAX,0x22),SHRT_MAX);
    check(packet(std::vector<BYTE>(SHRT_MAX,0x22),{}),0,0,0,100,0,3);
    uint32_t random=0x9ba72531;
    for(unsigned trial=0;trial<10000;++trial) {
        std::vector<BYTE> data;
        for(unsigned i=0;i<trial%64;++i){random^=random<<13;random^=random>>17;random^=random<<5;data.push_back(static_cast<BYTE>(random>>8));}
        check(data,static_cast<short>(data.size()),static_cast<short>(random),static_cast<short>(random>>16),
            static_cast<short>(random>>8),0,(trial%16)&~2,trial%1000==0);
    }
#if HDMV_NATIVE_PIXELS
    for(int type:{0,static_cast<int>(MSP_AYUV_PLANAR)})for(short x:{static_cast<short>(0),static_cast<short>(1),static_cast<short>(15),static_cast<short>(16)})
        for(short y:{static_cast<short>(0),static_cast<short>(1),static_cast<short>(7)})for(int flags:{2,3,7,11,19}) {
            check(packet(mixed,mixed),0,x,y,300,0,flags,true,type);
            check(mixed,static_cast<short>(mixed.size()),x,y,300,0,flags&~2,true,type);
        }
#endif
    printf("%llu exact DVB field comparisons passed (%llu native observations, %llu injected boundary exceptions, %llu native pixel comparisons, %llu visible pixel cases)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(observations),static_cast<unsigned long long>(exceptions),
        static_cast<unsigned long long>(pixels),static_cast<unsigned long long>(visible));
}
