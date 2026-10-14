// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "xy_rust_bilinear.h"
typedef uint16_t WORD;
size_t g_xy_malloc_used_size=0;
static uint64_t bilinearOwners=0,bilinearLoans=0,bilinearReleases=0;
static void* bilinearOwner=nullptr;
static bool bilinearFail=false;
static size_t bilinearAllocationSize=0;
static void* bilinearAllocation=nullptr;
static std::vector<uint64_t> bilinearEvents;
static std::vector<WORD> bilinearHistory;
static void* xy_malloc(size_t size) {
    bilinearEvents.push_back(size);
    if(bilinearAllocation){std::abort();}
    if(bilinearFail || size>65536) {bilinearEvents.push_back(0);return nullptr;}
    bilinearAllocationSize=size;
    bilinearAllocation=std::malloc(size ? size : 1);
    if(!bilinearAllocation)std::abort();
    bilinearEvents.push_back(1);g_xy_malloc_used_size+=size;
    return bilinearAllocation;
}
static void xy_free(void* pointer) {
    if(pointer!=bilinearAllocation || !pointer)std::abort();
    bilinearEvents.push_back(2);
    const WORD* history=static_cast<const WORD*>(pointer);
    bilinearHistory.insert(bilinearHistory.end(),history,history+bilinearAllocationSize/sizeof(WORD));
    g_xy_malloc_used_size-=bilinearAllocationSize;std::free(pointer);bilinearAllocation=nullptr;
}

extern "C" {
void* xy_vsfilter_plane_create(size_t);
uint8_t* xy_vsfilter_plane_loan(void*);
void xy_vsfilter_plane_release(void*);
}
extern "C" inline void* bilinearPlaneCreate(size_t count){
    if(bilinearFail||count>65536)return nullptr;
    void* owner=xy_vsfilter_plane_create(count);
    if(owner){
        if(bilinearOwner||bilinearAllocation)std::abort();
        bilinearEvents.push_back(count);bilinearEvents.push_back(1);
        bilinearAllocationSize=count;bilinearOwner=owner;++bilinearOwners;
    }
    return owner;
}
extern "C" inline uint8_t* bilinearPlaneLoan(void* owner){
    if(owner!=bilinearOwner)std::abort();
    uint8_t* data=xy_vsfilter_plane_loan(owner);if(reinterpret_cast<uintptr_t>(data)%16)std::abort();
    bilinearAllocation=data;++bilinearLoans;return data;
}
extern "C" inline void bilinearPlaneRelease(void* owner){
    if(owner!=bilinearOwner||!bilinearAllocation)std::abort();
    bilinearEvents.push_back(2);
    const WORD* history=static_cast<const WORD*>(bilinearAllocation);
    bilinearHistory.insert(bilinearHistory.end(),history,history+bilinearAllocationSize/sizeof(WORD));
    xy_vsfilter_plane_release(owner);bilinearAllocation=nullptr;bilinearOwner=nullptr;++bilinearReleases;
}
#define xy_vsfilter_plane_create bilinearPlaneCreate
#define xy_vsfilter_plane_loan bilinearPlaneLoan
#define xy_vsfilter_plane_release bilinearPlaneRelease
#include "xy_rust_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
