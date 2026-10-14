// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_cc_packet.h"
static uint64_t histories=0,observations=0,exclusions=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
static void effects(){int e=errno,f=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
unsigned long last=GetLastError();
#else
unsigned long last=0;
#endif
packetEvents.insert(packetEvents.end(),{static_cast<uint64_t>(e),static_cast<uint64_t>(f),last});}
static bool fits(const std::vector<BYTE>& data,int length){
    if(length<0)return length>=-100;
    if(static_cast<size_t>(length)>data.size())return false;
    for(int i=0;i<length-9;++i){
        const BYTE marker[]={0,0,1,0xb2,0x43,0x43,1,0xf8};
        if(std::memcmp(data.data()+i,marker,8)!=0)continue;
        i+=8;int count=data[i++]&63;count=(count+1)&~1;
        for(int j=0;j<count&&i<0x800;){
            if(static_cast<size_t>(i)+3>data.size())return false;i+=3;++j;
            if(j>=count)break;
            if(static_cast<size_t>(i)+3>data.size())return false;i+=3;++j;
        }
        break;
    }
    return true;
}
template<class Decoder>static std::vector<uint64_t> execute(const std::vector<BYTE>& input,int length,unsigned decline){
    packetRequire(packetRegions.empty());auto data=input;packetInput=data.data();packetBacking=data.size();packetDeclineMask=decline;packetEvents.clear();Decoder decoder;
    for(int repeat=0;repeat<2;++repeat){packetAllocation=0;seed();decoder.ExtractCC(data.data(),length,-1234567890123LL+repeat);effects();packetRequire(data==input&&packetRegions.empty());}
    return packetEvents;
}
static void compare(const std::vector<BYTE>& input,int length,unsigned decline){
    if(!fits(input,length)){++exclusions;return;}
    auto a=execute<ReferencePacket::CCDecoder>(input,length,decline);auto b=execute<CandidatePacket::CCDecoder>(input,length,decline);
    if(a!=b){fprintf(stderr,"Caption packet mismatch history=%llu length=%d decline=%u\n",(unsigned long long)histories,length,decline);std::abort();}
    ++histories;observations+=a.size();
}
int main(){
    const BYTE marker[]={0,0,1,0xb2,0x43,0x43,1,0xf8};
    for(int offset:{0,1,3,16,1900,2035,2038,2040,2041,2047,2050})for(int count=0;count<256;++count)for(int pattern=0;pattern<4;++pattern){
        std::vector<BYTE> data(2304,0x23);std::memcpy(data.data()+offset,marker,8);data[offset+8]=static_cast<BYTE>(count);
        for(int j=0;j<64;++j){const size_t i=offset+9+3*j;data[i]=pattern==0?0:pattern==1?255:pattern==2?(j&1?255:0):(j%3?255:0);data[i+1]=static_cast<BYTE>(j*37+count);data[i+2]=static_cast<BYTE>(j*53-count);}
        for(int length:{offset+9,offset+10,2048,2250})compare(data,length,0);
        if(count%31==0)for(unsigned decline:{1,2,3})compare(data,2250,decline);
    }
    for(int length:{-100,-1,0,1,8,9,10,2048})compare(std::vector<BYTE>(2048,0xff),length,0);
    // The first marker stops scanning even if its count is zero.
    for(int count:{0,1,63,255}){std::vector<BYTE> data(2048,0x23);std::memcpy(data.data()+7,marker,8);data[15]=static_cast<BYTE>(count);std::memcpy(data.data()+300,marker,8);data[308]=63;compare(data,2048,0);}
    // Reject short physical backing before running either implementation.
    std::vector<BYTE> shortInput(11,0xff);std::memcpy(shortInput.data(),marker,8);shortInput[8]=63;compare(shortInput,11,0);
    packetRequire(packetPlans&&packetFallbacks&&histories&&observations&&exclusions&&packetDecodes&&packetInitialized&&packetUnwritten&&packetOwners&&packetOwners==packetLoans&&packetLoans==packetReleases&&packetDeclines);
    printf("%llu exact caption packet histories / %llu allocation, callback and state observations passed (%llu decoder callbacks / %llu initialized bytes / %llu unwritten cells tracked without reads across both sides / %llu Rust owners, loans and releases / %llu modeled Rust allocation declines / %llu Rust packet plans / %llu bounded native parsing fallbacks; %llu short-backing histories excluded before both and NOT passes; whole pinned ExtractCC, first-marker stop, all count bytes, odd-count rounding, field ordering, 0x800 cursor cutoff, logical lengths with larger initialized physical backing, unchanged source, callback byte mutation, repeated calls and errno/FP/Windows error state; native unaligned DWORD loads normalized to checked memcpy, allocation and DecodeCC sink controlled, actual allocation failure and callback exceptions excluded)\n",(unsigned long long)histories,(unsigned long long)observations,(unsigned long long)packetDecodes,(unsigned long long)packetInitialized,(unsigned long long)packetUnwritten,(unsigned long long)packetOwners,(unsigned long long)packetDeclines,(unsigned long long)packetPlans,(unsigned long long)packetFallbacks,(unsigned long long)exclusions);
}
