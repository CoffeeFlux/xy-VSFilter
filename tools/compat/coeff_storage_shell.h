// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfenv>
#include <cerrno>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
static void coeffRequire(bool value){if(!value){fprintf(stderr,"Coefficient fixture outside scope\n");std::abort();}}
#define ASSERT(x) coeffRequire(x)
size_t g_xy_malloc_used_size=0;
struct CoeffRegion {uint8_t* data;uint8_t* raw;void* owner;size_t count;bool initialized;};
static std::vector<CoeffRegion> coeffRegions;
static std::vector<uint64_t> coeffEvents;
static bool coeffDecline=false,coeffFail=false;
static size_t coeffRequested=0;
static uint64_t coeffOwners=0,coeffLoans=0,coeffReleases=0,coeffDeclines=0;
static CoeffRegion& coeffRegion(const void* p){for(auto& r:coeffRegions)if(r.data==p)return r;coeffRequire(false);return coeffRegions[0];}
static void coeffGuards(const CoeffRegion& r){if(r.raw){for(size_t i=0;i<32;++i)coeffRequire(r.data[-static_cast<std::ptrdiff_t>(i)-1]==0xa7&&r.data[r.count+i]==0xa7);}}
static void* xy_malloc(size_t count){
    coeffRequire(count>0&&count<100000);coeffEvents.insert(coeffEvents.end(),{1,count});
    if(coeffFail){coeffEvents.push_back(3);return NULL;}
    auto* raw=static_cast<uint8_t*>(std::malloc(count+79));coeffRequire(raw!=NULL);
    auto* data=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(raw+32)+15)&~uintptr_t(15));
    std::memset(data-32,0xa7,count+64);coeffRegions.push_back({data,raw,NULL,count,false});g_xy_malloc_used_size+=count;return data;
}
static void xy_free(void* p){
    if(!p){coeffEvents.push_back(0);return;}
    for(size_t i=0;i<coeffRegions.size();++i)if(coeffRegions[i].data==p){auto r=coeffRegions[i];coeffRequire(r.raw&&!r.owner);coeffGuards(r);coeffEvents.insert(coeffEvents.end(),{2,r.count});g_xy_malloc_used_size-=r.count;std::free(r.raw);coeffRegions.erase(coeffRegions.begin()+i);return;}
    coeffRequire(false);
}
#include "xy_rust_plane.h"
static void* coeffCreate(size_t count){
    coeffRequire(count==coeffRequested+16);
    if(coeffDecline||coeffFail){++coeffDeclines;return NULL;}
    void* owner=xy_vsfilter_plane_create(count);coeffRequire(owner!=NULL);
    coeffRegions.push_back({NULL,NULL,owner,coeffRequested,false});++coeffOwners;return owner;
}
static uint8_t* coeffLoan(void* owner){
    auto* base=xy_vsfilter_plane_loan(owner);coeffRequire(reinterpret_cast<uintptr_t>(base)%16==0);
    for(auto& r:coeffRegions)if(r.owner==owner){coeffRequire(!r.data);r.data=base+16;coeffEvents.insert(coeffEvents.end(),{1,r.count});++coeffLoans;return base;}
    coeffRequire(false);return NULL;
}
static void coeffRelease(void* owner){
    if(!owner){xy_vsfilter_plane_release(owner);return;}
    for(size_t i=0;i<coeffRegions.size();++i)if(coeffRegions[i].owner==owner){coeffEvents.insert(coeffEvents.end(),{2,coeffRegions[i].count});coeffRegions.erase(coeffRegions.begin()+i);xy_vsfilter_plane_release(owner);++coeffReleases;return;}
    coeffRequire(false);
}
#define xy_vsfilter_plane_create coeffCreate
#define xy_vsfilter_plane_loan coeffLoan
#define xy_vsfilter_plane_release coeffRelease
#include "xy_rust_shifted_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
namespace XyRust {
class ObservedCoefficientStorage:public ShiftedPlaneStorage {public:bool AllocateWithPrefix(size_t count,unsigned prefix){coeffRequire(prefix==16);coeffRequested=count;return ShiftedPlaneStorage::AllocateWithPrefix(count,prefix);}};
}
static void* coeffCopy(void* dst,const void* src,size_t count){
    auto& a=coeffRegion(dst);auto& b=coeffRegion(src);coeffRequire(dst!=src&&a.count==count&&b.count>=count&&b.initialized);coeffEvents.insert(coeffEvents.end(),{4,count});std::memcpy(dst,src,count);a.initialized=true;return dst;
}
