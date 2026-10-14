// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cerrno>
#include <cfenv>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#else
typedef uint8_t BYTE;
#define __int64 int64_t
#endif
static void packetRequire(bool ok){if(!ok){fprintf(stderr,"Caption packet fixture outside scope\n");std::abort();}}
struct PacketRegion {BYTE* data;BYTE* raw;void* owner;size_t count;std::vector<bool> initialized;};
static std::vector<PacketRegion> packetRegions;
static std::vector<uint64_t> packetEvents;
static const BYTE* packetInput=NULL;
static size_t packetBacking=0;
static unsigned packetAllocation=0,packetDeclineMask=0;
static uint64_t packetPlans=0,packetFallbacks=0,packetWordReads=0,packetNativeWrites=0;
static uint64_t packetOwners=0,packetLoans=0,packetReleases=0,packetDeclines=0,packetDecodes=0,packetInitialized=0,packetUnwritten=0;
static PacketRegion& packetRegion(const void* p){for(auto& r:packetRegions)if(r.data==p)return r;packetRequire(false);return packetRegions[0];}
static uint32_t packetWord(const BYTE* p){
    const auto offset=p-packetInput;packetRequire(offset>=0&&static_cast<size_t>(offset)+4<=packetBacking);
    uint32_t value=0;std::memcpy(&value,p,4);++packetWordReads;return value;
}
static void packetPut(BYTE* p,size_t index,BYTE value){auto& r=packetRegion(p);packetRequire(index<r.count);p[index]=value;r.initialized[index]=true;++packetNativeWrites;}
static void packetSnapshot(const PacketRegion& r){
    packetEvents.push_back(r.count);
    for(size_t i=0;i<r.count;++i){packetEvents.push_back(r.initialized[i]);if(r.initialized[i]){packetEvents.push_back(r.data[i]);++packetInitialized;}else ++packetUnwritten;}
    if(r.raw)for(size_t i=0;i<16;++i)packetRequire(r.raw[i]==0x91&&r.data[r.count+i]==0x91);
}
static BYTE* packetNew(size_t count){packetRequire(count>0&&count<=64);auto* raw=new BYTE[count+32];std::memset(raw,0x91,count+32);packetRegions.push_back({raw+16,raw,NULL,count,std::vector<bool>(count,false)});packetEvents.insert(packetEvents.end(),{1,count});++packetAllocation;return raw+16;}
static void packetDelete(BYTE* p){for(size_t i=0;i<packetRegions.size();++i)if(packetRegions[i].data==p){const auto r=packetRegions[i];packetRequire(r.raw&&!r.owner);packetEvents.push_back(2);packetSnapshot(r);delete[] r.raw;packetRegions.erase(packetRegions.begin()+i);return;}packetRequire(false);}
#include "xy_rust_plane.h"
static void* packetCreate(size_t count){
    if(packetDeclineMask&(1U<<packetAllocation)){++packetDeclines;return NULL;}
    void* owner=xy_vsfilter_plane_create(count);packetRequire(owner!=NULL);packetRegions.push_back({NULL,NULL,owner,count,std::vector<bool>(count,false)});++packetOwners;++packetAllocation;return owner;
}
static uint8_t* packetLoan(void* owner){auto* data=xy_vsfilter_plane_loan(owner);for(auto& r:packetRegions)if(r.owner==owner){packetRequire(!r.data);r.data=data;packetEvents.insert(packetEvents.end(),{1,r.count});++packetLoans;return data;}packetRequire(false);return NULL;}
static void packetRelease(void* owner){if(!owner){xy_vsfilter_plane_release(owner);return;}for(size_t i=0;i<packetRegions.size();++i)if(packetRegions[i].owner==owner){packetEvents.push_back(2);packetSnapshot(packetRegions[i]);packetRegions.erase(packetRegions.begin()+i);xy_vsfilter_plane_release(owner);++packetReleases;return;}packetRequire(false);}
#define xy_vsfilter_plane_create packetCreate
#define xy_vsfilter_plane_loan packetLoan
#define xy_vsfilter_plane_release packetRelease
#include "xy_rust_byte_storage.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
#include "xy_rust_cc.h"
namespace XyRust {
class ObservedCaptionPacket:public CaptionPacket {public:
    ObservedCaptionPacket(const uint8_t* input,int length):CaptionPacket(input,length){if(Handled())++packetPlans;else ++packetFallbacks;}
    void Copy(uint8_t* first,uint8_t* second)const{
        CaptionPacket::Copy(first,second);
        unsigned field=0;for(auto* data:{first,second}){auto& r=packetRegion(data);const int count=Length(field++);packetRequire(count>=0&&static_cast<size_t>(count)<=r.count);for(int i=0;i<count;++i)r.initialized[i]=true;}
    }
};
}
static void packetDecode(BYTE* bytes,int count,int64_t time){
    auto& r=packetRegion(bytes);packetRequire(count>0&&static_cast<size_t>(count)<=r.count);packetEvents.insert(packetEvents.end(),{3,static_cast<uint64_t>(count),static_cast<uint64_t>(time)});
    for(int i=0;i<count;++i){packetRequire(r.initialized[i]);packetEvents.push_back(bytes[i]);bytes[i]^=0x80;}
    ++packetDecodes;
}
#define PACKET_CLASS class CCDecoder {public:void ExtractCC(BYTE*,int,int64_t);void DecodeCC(BYTE* p,int n,int64_t t){packetDecode(p,n,t);}};
