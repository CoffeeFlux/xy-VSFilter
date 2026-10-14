// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cassert>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <mmintrin.h>
#else
inline void _mm_empty(){}
#endif
#include "original_be_filter.h"
#include "original_transpose.h"
#undef ASSERT
#define ASSERT(x) assert(x)
typedef UINT* PUINT;
typedef uint8_t UINT8,*PUINT8;
typedef const uint8_t* PCUINT8;
size_t g_xy_malloc_used_size=0;
static std::vector<uint64_t> fractionEvents,fractionHistory;
static std::vector<bool> fractionWritten;
static uint8_t* fractionData=NULL;
static uint8_t* fractionNativeBase=NULL;
static size_t fractionCount=0;
static void* fractionOwner=NULL;
static uint64_t fractionOwners=0,fractionLoans=0,fractionReleases=0,fractionStages=0,fractionDeclines=0;
static bool fractionDecline=false;
static uint8_t* fractionInput=NULL;
static size_t fractionInputCount=0;
static size_t checkedOffset(const void* value,const void* base,size_t length,size_t count){
    const uintptr_t p=reinterpret_cast<uintptr_t>(value),b=reinterpret_cast<uintptr_t>(base);
    if(!base||p<b||p-b>length||count>length-(p-b)){fprintf(stderr,"Fractionian callback outside modeled extent\n");std::abort();}
    return static_cast<size_t>(p-b);
}
static void beginScratch(size_t count){
    if(fractionData||fractionOwner||count==0)std::abort();
    fractionCount=count;fractionWritten.assign(count,false);fractionEvents.insert(fractionEvents.end(),{1,count});
}
static void captureScratch(){
    fractionEvents.push_back(2);
    for(size_t i=0;i<fractionCount;++i)if(fractionWritten[i]){fractionHistory.push_back(i);fractionHistory.push_back(fractionData[i]);}
    // Unwritten scratch bytes are neither read nor compared.
}
static void* xy_malloc(size_t count){
    beginScratch(count);fractionNativeBase=static_cast<uint8_t*>(std::malloc(count+96));if(!fractionNativeBase)std::abort();
    std::memset(fractionNativeBase,0xcc,count+96);fractionData=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(fractionNativeBase+32)+15)&~uintptr_t(15));g_xy_malloc_used_size+=count;return fractionData;
}
static void xy_free(void* p){
    if(p!=fractionData||fractionOwner||!fractionNativeBase)std::abort();captureScratch();
    const size_t offset=static_cast<size_t>(fractionData-fractionNativeBase);
    for(size_t i=0;i<fractionCount+96;++i)if((i<offset||i>=offset+fractionCount)&&fractionNativeBase[i]!=0xcc)std::abort();
    g_xy_malloc_used_size-=fractionCount;std::free(fractionNativeBase);fractionNativeBase=fractionData=NULL;
}
extern "C" {
void* xy_vsfilter_plane_create(size_t);
uint8_t* xy_vsfilter_plane_loan(void*);
void xy_vsfilter_plane_release(void*);
}
extern "C" inline void* fractionPlaneCreate(size_t count){
    if(fractionDecline){++fractionDeclines;return NULL;}
    void* owner=xy_vsfilter_plane_create(count);if(owner){beginScratch(count);fractionOwner=owner;++fractionOwners;}return owner;
}
extern "C" inline uint8_t* fractionPlaneLoan(void* owner){
    if(owner!=fractionOwner)std::abort();fractionData=xy_vsfilter_plane_loan(owner);
    if(reinterpret_cast<uintptr_t>(fractionData)%16)std::abort();++fractionLoans;return fractionData;
}
extern "C" inline void fractionPlaneRelease(void* owner){
    if(owner!=fractionOwner)std::abort();captureScratch();xy_vsfilter_plane_release(owner);fractionOwner=NULL;fractionData=NULL;++fractionReleases;
}
#define xy_vsfilter_plane_create fractionPlaneCreate
#define xy_vsfilter_plane_loan fractionPlaneLoan
#define xy_vsfilter_plane_release fractionPlaneRelease
#include "xy_rust_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release

static void observeRows(const uint8_t* data,int width,int height,int pitch,bool written){
    const bool scratch=data==fractionData;
    if(!scratch && data!=fractionInput)std::abort();
    const size_t count=scratch?fractionCount:fractionInputCount;
    for(int y=0;y<height;++y){
        checkedOffset(data+y*pitch,data,count,width);
        for(int x=0;x<width;++x){
            const size_t offset=static_cast<size_t>(y)*pitch+x;
            if(scratch){if(written)fractionWritten[offset]=true;else if(!fractionWritten[offset])std::abort();}
            fractionHistory.push_back(data[offset]);
        }
    }
}
static void filterStage(uint8_t* data,int width,int height,int pitch,const UINT* filter,bool after){
    fractionEvents.insert(fractionEvents.end(),{3,static_cast<uint64_t>(after),data==fractionData?1u:0u,static_cast<uint64_t>(width),static_cast<uint64_t>(height),static_cast<uint64_t>(pitch),g_xy_malloc_used_size});
    if(filter)fractionEvents.insert(fractionEvents.end(),{filter[0],filter[1],filter[2]});
    observeRows(data,width,height,pitch,false);++fractionStages;
}
static void transposeBefore(uint8_t* dst,int dw,int ds,const uint8_t* src,int w,int h,int ss){
    fractionEvents.insert(fractionEvents.end(),{4,dst==fractionData?1u:0u,static_cast<uint64_t>(dw),static_cast<uint64_t>(ds),static_cast<uint64_t>(w),static_cast<uint64_t>(h),static_cast<uint64_t>(ss),g_xy_malloc_used_size});
    observeRows(src,w,h,ss,false);
}
static void transposeAfter(uint8_t* dst,int dw,int ds,int w){observeRows(dst,dw,w,ds,true);++fractionStages;}
