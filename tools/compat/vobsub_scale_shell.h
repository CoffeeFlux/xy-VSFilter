// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cfenv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include "xy_rust_vobsub.h"
#include "xy_rust_plane.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
typedef uint32_t DWORD;
#endif
struct CRect {
    int32_t left,top,right,bottom;
    CRect(int32_t l,int32_t t,int32_t r,int32_t b):left(l),top(t),right(r),bottom(b){}
    CRect():left(0),top(0),right(0),bottom(0){}
    int32_t Width()const{return right-left;}
    int32_t Height()const{return bottom-top;}
};
struct ScaleAllocation {};
struct ScaleInterrupted {};
struct ScaleRegion {void* owner;uint8_t* data;uint8_t* raw;size_t count;};
static int scaleFailure=-1;
static bool scaleDecline=false;
static std::vector<size_t> scaleAllocations;
static std::vector<int64_t> scaleEvents;
static std::vector<ScaleRegion> scaleRegions;
static uint64_t scaleHandled=0,scaleFallback=0,scaleOwners=0,scaleLoans=0,scaleReleases=0,scaleDeclines=0;
static void scaleRequire(bool value){if(!value)std::abort();}
static void scaleAllocateEvent(size_t bytes){scaleAllocations.push_back(bytes);scaleEvents.insert(scaleEvents.end(),{1,static_cast<int64_t>(bytes)});}
inline void* operator new[](size_t bytes,ScaleAllocation) {
    scaleAllocateEvent(bytes);
    if(static_cast<int>(scaleAllocations.size())-1==scaleFailure)throw ScaleInterrupted();
    uint8_t* raw=static_cast<uint8_t*>(::operator new[](bytes+32));
    std::memset(raw,0xab,16);std::memset(raw+16+bytes,0xab,16);
    scaleRegions.push_back({NULL,raw+16,raw,bytes});return raw+16;
}
static void scaleDelete(void* memory){
    for(size_t i=0;i<scaleRegions.size();++i)if(scaleRegions[i].data==memory){
        const auto region=scaleRegions[i];scaleRequire(!region.owner&&region.raw);
        for(size_t j=0;j<16;++j)scaleRequire(region.raw[j]==0xab&&region.data[region.count+j]==0xab);
        scaleEvents.insert(scaleEvents.end(),{2,static_cast<int64_t>(region.count)});
        ::operator delete[](region.raw);scaleRegions.erase(scaleRegions.begin()+i);return;
    }
    scaleRequire(false);
}
inline void operator delete[](void* memory,ScaleAllocation) noexcept {scaleDelete(memory);}
static void* observedScaleCreate(size_t count){
    // A modeled native allocation interruption first makes Rust decline, then
    // occurs at the unchanged native fallback, before any source pixel access.
    if(scaleDecline||static_cast<int>(scaleAllocations.size())==scaleFailure){++scaleDeclines;return NULL;}
    void* owner=xy_vsfilter_plane_create(count);scaleRequire(owner!=NULL);
    scaleRegions.push_back({owner,NULL,NULL,count});++scaleOwners;return owner;
}
static uint8_t* observedScaleLoan(void* owner){
    uint8_t* data=xy_vsfilter_plane_loan(owner);
    for(auto& region:scaleRegions)if(region.owner==owner){scaleRequire(!region.data&&data);region.data=data;scaleAllocateEvent(region.count);++scaleLoans;return data;}
    scaleRequire(false);return NULL;
}
static void observedScaleRelease(void* owner){
    if(!owner){xy_vsfilter_plane_release(owner);return;}
    for(size_t i=0;i<scaleRegions.size();++i)if(scaleRegions[i].owner==owner){
        scaleEvents.insert(scaleEvents.end(),{2,static_cast<int64_t>(scaleRegions[i].count)});
        scaleRegions.erase(scaleRegions.begin()+i);xy_vsfilter_plane_release(owner);++scaleReleases;return;
    }
    scaleRequire(false);
}
#define xy_vsfilter_plane_create observedScaleCreate
#define xy_vsfilter_plane_loan observedScaleLoan
#define xy_vsfilter_plane_release observedScaleRelease
#include "xy_rust_byte_storage.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
#define DEBUG_NEW new (ScaleAllocation{})
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define VOBSUB_SCALE_SHELL(Name) \
class Name { public: CRect rect;DWORD* lpPixels;void Scale2x(); };
static uint32_t observedScale(const uint32_t* source,uint32_t* destination,int32_t width,int32_t height) {
    bool found=false;for(const auto& region:scaleRegions)if(region.data==reinterpret_cast<uint8_t*>(destination)){scaleRequire(region.count==static_cast<size_t>(width)*height*4);found=true;}scaleRequire(found);
    const auto result=xy_vsfilter_vobsub_scale(source,destination,width,height);scaleHandled+=result!=0;scaleFallback+=result==0;return result;
}
#define xy_vsfilter_vobsub_scale observedScale
