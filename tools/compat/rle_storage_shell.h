// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
typedef uint8_t BYTE;
typedef uint32_t DWORD;
#endif
#include "xy_rust_plane.h"
struct RleRegion {
    BYTE* data;size_t count;BYTE* allocation;void* owner;
    std::vector<bool> initialized;
};
static std::vector<RleRegion> rleRegions;
static std::vector<int64_t> rleEvents;
static bool rleDecline=false;
static size_t rleAllocationLimit=8192;
static uint64_t rleOwners=0,rleLoans=0,rleReleases=0,rleDeclines=0,rleAssertions=0;
static void requireRle(bool value){if(!value){fprintf(stderr,"Invalid RLE storage fixture or ownership\n");std::abort();}}
static RleRegion& rleRegion(const void* p,size_t count,size_t& offset){
    const uintptr_t address=reinterpret_cast<uintptr_t>(p);
    for(auto& region:rleRegions){const uintptr_t start=reinterpret_cast<uintptr_t>(region.data);
        if(address>=start&&address-start<=region.count&&count<=region.count-(address-start)){
            offset=static_cast<size_t>(address-start);return region;
        }
    }
    requireRle(false);return rleRegions[0];
}
static void rleGuards(const RleRegion& region){
    if(region.allocation)for(size_t i=0;i<16;++i){requireRle(region.allocation[i]==0xab);requireRle(region.data[region.count+i]==0xab);}
}
static BYTE* rleAllocate(size_t count){
    requireRle(count<=rleAllocationLimit);BYTE* raw=new BYTE[count+32];std::memset(raw,0xab,count+32);
    rleRegions.push_back({raw+16,count,raw,NULL,std::vector<bool>(count,false)});
    rleEvents.insert(rleEvents.end(),{1,static_cast<int64_t>(count)});return raw+16;
}
static void rleRelease(BYTE* data){
    if(!data)return;
    for(size_t i=0;i<rleRegions.size();++i)if(rleRegions[i].data==data){
        const auto region=rleRegions[i];requireRle(region.allocation!=NULL&&!region.owner);rleGuards(region);
        rleEvents.insert(rleEvents.end(),{2,static_cast<int64_t>(region.count)});
        delete[] region.allocation;rleRegions.erase(rleRegions.begin()+i);return;
    }
    requireRle(false);
}
static void* observedRleCreate(size_t count){
    if(rleDecline&&count){++rleDeclines;return NULL;}
    void* owner=xy_vsfilter_plane_create(count);
    if(owner){++rleOwners;rleRegions.push_back({NULL,count,NULL,owner,std::vector<bool>(count,false)});}
    return owner;
}
static BYTE* observedRleLoan(void* owner){
    BYTE* data=xy_vsfilter_plane_loan(owner);
    for(auto& region:rleRegions)if(region.owner==owner){requireRle(region.data==NULL);region.data=data;++rleLoans;rleEvents.insert(rleEvents.end(),{1,static_cast<int64_t>(region.count)});return data;}
    requireRle(false);return NULL;
}
static void observedRleRelease(void* owner){
    if(!owner){xy_vsfilter_plane_release(owner);return;}
    for(size_t i=0;i<rleRegions.size();++i)if(rleRegions[i].owner==owner){
        rleEvents.insert(rleEvents.end(),{2,static_cast<int64_t>(rleRegions[i].count)});
        rleRegions.erase(rleRegions.begin()+i);xy_vsfilter_plane_release(owner);++rleReleases;return;
    }
    requireRle(false);
}
#define xy_vsfilter_plane_create observedRleCreate
#define xy_vsfilter_plane_loan observedRleLoan
#define xy_vsfilter_plane_release observedRleRelease
#include "xy_rust_byte_storage.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
static void* rleCopy(void* destination,const void* source,size_t count){
    // Copy's palette array is initialized separately and has a fixed footprint.
    size_t a,b;auto& dst=rleRegion(destination,count,a);const auto& src=rleRegion(source,count,b);
    requireRle(&dst!=&src||a+count<=b||b+count<=a);
    for(size_t i=0;i<count;++i)requireRle(src.initialized[b+i]);
    rleEvents.insert(rleEvents.end(),{3,static_cast<int64_t>(a),static_cast<int64_t>(b),static_cast<int64_t>(count)});
    std::memcpy(destination,source,count);for(size_t i=0;i<count;++i)dst.initialized[a+i]=true;return destination;
}
static void rleAssert(bool value){rleEvents.insert(rleEvents.end(),{4,value?1:0});rleAssertions+=!value;}
#define ASSERT(x) rleAssert(x)
#define DEBUG_NEW new
static void memsetd(void* dst,DWORD value,size_t count){requireRle(value==0);std::memset(dst,0,count);}
struct HDMV_PALETTE {BYTE entry_id,Y,Cr,Cb,T;};
struct RlePalette {
    std::vector<HDMV_PALETTE> data;
    void SetCount(size_t n){data.resize(n);}
    int GetCount()const{return static_cast<int>(data.size());}
    HDMV_PALETTE& operator[](size_t i){return data.at(i);}
};
#define RLE_OTHER_FIELDS \
    RlePalette m_Palette;int m_OriginalColorType,m_OriginalYuvRangeType; \
    DWORD m_Colors[256];int m_colorType;
