// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_packet.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
static uint64_t comparisons=0,observations=0,interruptions=0,aliases=0;
struct Outcome {
    std::vector<std::vector<uint64_t>> checkpoints,events;
    bool interrupted;
    bool operator==(const Outcome& other)const {return checkpoints==other.checkpoints && events==other.events && interrupted==other.interrupted;}
};
template<class Image> static Outcome execute(const std::vector<BYTE>& data,size_t length,size_t dataSize,int time,unsigned seed,int failure,bool alias) {
    Image image;
    image.guard0=0xdeadbeef01234567ULL;image.guard1=0xfedcba9876543210ULL;
    image.nOffset[0]=static_cast<size_t>(0x1234567812345678ULL+seed);image.nOffset[1]=static_cast<size_t>(0xfedcba9876543210ULL-seed);
    image.fForced=seed&1;image.fAnimated=seed&2;image.tCurrent=-17;
    image.start=-123456789012LL;image.delay=234567890123LL;
    image.rect=CRect(-16,3,17,-5);
    for(int i=0;i<4;++i){image.pal[i].pal=(i*3+seed)&15;image.pal[i].tr=(i*5+seed)&15;}
    const BYTE* input=data.data();
    if(alias){std::memcpy(&image.rect,data.data(),16);input=reinterpret_cast<const BYTE*>(&image.rect);}
    packetEvents.clear();packetFailure=failure;
    Outcome result;result.interrupted=false;
    for(int requested:{time,-1,time}) {
        try {image.GetPacketInfo(input,length,dataSize,requested);}
        catch(const PacketInterrupted&){result.interrupted=true;}
        result.checkpoints.push_back(packetState(image));
        if(result.interrupted)break;
    }
    result.events=packetEvents;
    if(image.guard0!=0xdeadbeef01234567ULL || image.guard1!=0xfedcba9876543210ULL)std::abort();
    return result;
}
static void compare(const std::vector<BYTE>& data,size_t length,size_t dataSize,int time,unsigned seed=0,int failure=-1,bool alias=false) {
    const auto a=execute<ReferenceVobSub>(data,length,dataSize,time,seed,failure,alias);
    const auto b=execute<CandidateVobSub>(data,length,dataSize,time,seed,failure,alias);
    if(!(a==b)) {
        fprintf(stderr,"VobSub packet mismatch length=%zu dataSize=%zu time=%d seed=%u failure=%d alias=%d\n",length,dataSize,time,seed,failure,alias);std::abort();
    }
    ++comparisons;observations+=a.checkpoints.size()+a.events.size();interruptions+=a.interrupted;aliases+=alias;
}
static std::vector<BYTE> packet(unsigned ticks,const std::vector<BYTE>& commands) {
    std::vector<BYTE> result={static_cast<BYTE>(ticks>>8),static_cast<BYTE>(ticks),0,0};
    result.insert(result.end(),commands.begin(),commands.end());return result;
}
static uint32_t randomState=0x13579bdfU;
static uint32_t nextRandom(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
int main() {
    for(unsigned value=0;value<65536;++value) {
        for(BYTE command:{BYTE(3),BYTE(4)}) {
            const auto bytes=packet(value,{command,static_cast<BYTE>(value>>8),static_cast<BYTE>(value),0xff});
            compare(bytes,bytes.size(),0,INT_MAX,value&3);
        }
        const BYTE hi=static_cast<BYTE>(value>>8),lo=static_cast<BYTE>(value);
        const auto bytes=packet(value,{0,2,5,hi,lo,hi,lo,hi,lo,6,lo,hi,hi,lo,3,hi,lo,4,lo,hi,0xff});
        compare(bytes,bytes.size(),0,INT_MAX,value&3);
    }
    for(unsigned ticks:{0,1,89,90,91,32767,65535})for(int time:{INT_MIN,-1,0,1,1012,1023,1024,1035,INT_MAX})for(unsigned seed=0;seed<4;++seed) {
        const auto bytes=packet(ticks,{0,1,0,2,3,0x12,0x34,4,0x56,0x78,0xff});
        compare(bytes,bytes.size(),0,time,seed);
    }
    for(size_t length=0;length<=64;++length)for(size_t dataSize:{size_t(0),size_t(1),size_t(4),size_t(7)})for(unsigned target:{0U,4U,5U,8U,16U,32U,64U,65535U})for(int time:{-1,0,INT_MAX}) {
        std::vector<BYTE> bytes(length+64,0xff);
        bytes[dataSize]=0;bytes[dataSize+1]=90;bytes[dataSize+2]=static_cast<BYTE>(target>>8);bytes[dataSize+3]=static_cast<BYTE>(target);
        if(target>=dataSize+5 && target+5<=length) {
            bytes[target]=0;bytes[target+1]=91;bytes[target+2]=static_cast<BYTE>(target>>8);bytes[target+3]=static_cast<BYTE>(target);bytes[target+4]=2;
        }
        compare(bytes,length,dataSize,time,static_cast<unsigned>(length)&3);
    }
    for(unsigned index=0;index<100000;++index) {
        const size_t length=nextRandom()%257,dataSize=nextRandom()%17;
        std::vector<BYTE> bytes(length+64,0xff);
        for(size_t i=0;i<length;++i)bytes[i]=static_cast<BYTE>(nextRandom());
        const unsigned target=index%3==0 ? static_cast<unsigned>(length) : static_cast<unsigned>(dataSize);
        bytes[dataSize+2]=static_cast<BYTE>(target>>8);bytes[dataSize+3]=static_cast<BYTE>(target);
        const int time=index%2 ? INT_MAX : -1;
        compare(bytes,length,dataSize,time,index&3);
    }
    for(BYTE command:{BYTE(3),BYTE(4)})for(unsigned repeats:{65535U,65536U,65537U}) {
        std::vector<BYTE> commands;
        for(unsigned i=0;i<repeats;++i){commands.push_back(command);commands.push_back(static_cast<BYTE>(i>>8));commands.push_back(static_cast<BYTE>(i));}
        commands.push_back(0xff);const auto bytes=packet(90,commands);
        compare(bytes,bytes.size(),0,INT_MAX);
    }
    for(unsigned command=0;command<256;++command)for(int failure:{0,1}) {
        auto bytes=packet(90,{0,2,static_cast<BYTE>(command),1,2,3,4,5,6,0xff});
        const size_t length=7;bytes.resize(64,0xff);
        compare(bytes,length,0,INT_MAX,command&3,failure);
        bytes[2]=0xff;bytes[3]=0xff;compare(bytes,length,0,INT_MAX,command&3,failure);
    }
    const std::vector<BYTE> self={0,0,0,0,5,0x01,0x10,0x12,0x02,0x20,0x23,0xff,0,1,2,0xff};
    for(unsigned seed=0;seed<4;++seed)compare(self,16,0,INT_MAX,seed,-1,true);
    const auto shared=execute<ReferenceVobSub>(self,16,0,INT_MAX,0,-1,true);
    const auto separate=execute<ReferenceVobSub>(self,16,0,INT_MAX,0,-1,false);
    if(shared==separate || !packetHandled || !packetFallback){fprintf(stderr,"VobSub packet alias/route controls failed\n");return 1;}
    printf("%llu exact VobSub packet comparisons passed (%llu state/diagnostic observations, %llu injected diagnostic interruptions, %llu object-alias cases, %llu Rust-handled calls, %llu native fallback calls; all palette/transparency words, rectangles/offsets, future times, control targets, counter wrapping and retained state)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(observations),static_cast<unsigned long long>(interruptions),static_cast<unsigned long long>(aliases),static_cast<unsigned long long>(packetHandled),static_cast<unsigned long long>(packetFallback));
}
