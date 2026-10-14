// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "xy_rust_be_blur.h"
typedef uint16_t WORD;
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#define BLUR_NATIVE_SSE 1
#include <emmintrin.h>
#else
#define BLUR_NATIVE_SSE 0
#endif
size_t g_xy_malloc_used_size=0;
static uint64_t blurOwners=0,blurLoans=0,blurReleases=0;
struct BlurAllocation { void* pointer; size_t size; void* owner; bool rust; };
static std::vector<BlurAllocation> blurAllocations;
static std::vector<uint64_t> blurEvents;
static std::vector<WORD> blurHistory;
static int blurFail=-1;
static void* xy_malloc(size_t size) {
    const size_t index=blurAllocations.size();
    blurEvents.push_back(0);blurEvents.push_back(size);
    void* pointer=nullptr;
    if(static_cast<int>(index)!=blurFail && size<=65536) {
        pointer=std::malloc(size ? size : 1);
        if(!pointer)std::abort();
    }
    blurAllocations.push_back({pointer,size,nullptr,false});blurEvents.push_back(pointer ? 1 : 0);if(pointer)g_xy_malloc_used_size+=size;
    return pointer;
}
static void xy_free(void* pointer) {
    for(size_t i=0;i<blurAllocations.size();++i)if(blurAllocations[i].pointer==pointer && pointer) {
        blurEvents.push_back(1);blurEvents.push_back(i);
        const WORD* words=static_cast<const WORD*>(pointer);
        blurHistory.insert(blurHistory.end(),words,words+blurAllocations[i].size/sizeof(WORD));
        if(blurAllocations[i].rust)std::abort();g_xy_malloc_used_size-=blurAllocations[i].size;std::free(pointer);blurAllocations[i].pointer=nullptr;return;
    }
    std::abort();
}

extern "C" {
void* xy_vsfilter_plane_create(size_t);
uint8_t* xy_vsfilter_plane_loan(void*);
void xy_vsfilter_plane_release(void*);
}
extern "C" inline void* blurPlaneCreate(size_t count){
    // Allocation-failure fixtures select the existing native path. Actual
    // allocator exhaustion while creating a Rust pair is a separate scope.
    if(blurFail>=0||count>65536)return nullptr;
    void* owner=xy_vsfilter_plane_create(count);
    if(owner){blurEvents.insert(blurEvents.end(),{0,count,1});blurAllocations.push_back({nullptr,count,owner,true});++blurOwners;}
    return owner;
}
extern "C" inline uint8_t* blurPlaneLoan(void* owner){
    uint8_t* data=xy_vsfilter_plane_loan(owner);if(reinterpret_cast<uintptr_t>(data)%16)std::abort();
    for(auto& slot:blurAllocations)if(slot.rust&&slot.owner==owner){slot.pointer=data;++blurLoans;return data;}
    std::abort();
}
extern "C" inline void blurPlaneRelease(void* owner){
    for(size_t i=0;i<blurAllocations.size();++i){auto& slot=blurAllocations[i];if(slot.rust&&slot.owner==owner){
        blurEvents.push_back(1);blurEvents.push_back(i);
        const WORD* words=static_cast<const WORD*>(slot.pointer);blurHistory.insert(blurHistory.end(),words,words+slot.size/sizeof(WORD));
        xy_vsfilter_plane_release(owner);slot.owner=nullptr;slot.pointer=nullptr;++blurReleases;return;
    }}
    std::abort();
}
#define xy_vsfilter_plane_create blurPlaneCreate
#define xy_vsfilter_plane_loan blurPlaneLoan
#define xy_vsfilter_plane_release blurPlaneRelease
#include "xy_rust_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
