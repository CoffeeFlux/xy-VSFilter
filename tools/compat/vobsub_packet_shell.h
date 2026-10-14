// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "xy_rust_vobsub.h"
typedef uint8_t BYTE;
typedef uint16_t WORD;
struct CRect {
    int32_t left,top,right,bottom;
    CRect(int32_t l=0,int32_t t=0,int32_t r=0,int32_t b=0):left(l),top(t),right(r),bottom(b){}
};
struct PacketInterrupted {};
static std::vector<std::vector<uint64_t>> packetEvents;
static int packetFailure=-1;
static uint64_t packetHandled=0,packetFallback=0;
template<class Image> static std::vector<uint64_t> packetState(const Image& image) {
    std::vector<uint64_t> result={image.guard0,image.nOffset[0],image.nOffset[1],image.fForced,image.fAnimated,
        static_cast<uint32_t>(image.tCurrent),static_cast<uint64_t>(image.start),static_cast<uint64_t>(image.delay),
        static_cast<uint32_t>(image.rect.left),static_cast<uint32_t>(image.rect.top),static_cast<uint32_t>(image.rect.right),static_cast<uint32_t>(image.rect.bottom)};
    for(int i=0;i<4;++i){result.push_back(image.pal[i].pal);result.push_back(image.pal[i].tr);}
    result.push_back(image.guard1);return result;
}
template<class Image> static void packetObserve(const Image& image,uint64_t action) {
    auto state=packetState(image);state.insert(state.begin(),action);packetEvents.push_back(state);
    if(packetFailure>=0 && packetEvents.size()==static_cast<size_t>(packetFailure+1))throw PacketInterrupted();
}
#define ASSERT(condition) do {if(!(condition))packetObserve(*this,2);}while(0)
#define TRACE(message) packetObserve(*this,1)
#define _T(message) message
#define VOBSUB_PACKET_SHELL(Name) \
class Name { public: \
    uint64_t guard0;size_t nOffset[2];bool fForced,fAnimated;int tCurrent;int64_t start,delay; \
    CRect rect;struct SubPal {BYTE pal:4,tr:4;};SubPal pal[4];uint64_t guard1; \
    void GetPacketInfo(const BYTE*,size_t,size_t,int); \
};
static uint32_t observedVobSubPacket(const uint8_t* packet,size_t length,size_t dataSize,int32_t time,XyVobSubPacketPatch* output) {
    const uint32_t result=xy_vsfilter_vobsub_packet(packet,length,dataSize,time,output);
    packetHandled+=result!=0;packetFallback+=result==0;return result;
}
#define xy_vsfilter_vobsub_packet observedVobSubPacket
