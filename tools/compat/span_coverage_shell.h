// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#else
typedef uint8_t BYTE;
#endif
#include "xy_rust_span_coverage.h"
namespace boost {namespace flyweights {}}
#define countof(x) (sizeof(x)/sizeof((x)[0]))
typedef std::pair<uint64_t,uint64_t> tSpan;
typedef std::vector<tSpan> tSpanBuffer;
struct ScanLineData {int mWidth,mHeight;tSpanBuffer mOutline;};
struct ScanLineData2 {
    std::shared_ptr<ScanLineData> m_scan_line_data;
    int mWideBorder,mPathOffsetX,mPathOffsetY;tSpanBuffer mWideOutline;
};
size_t g_xy_malloc_used_size=0;
static std::vector<int64_t> coverageEvents;
static int allocationAttempt=0,allocationFailure=-1;
struct CoverageAllocation {BYTE* base;size_t count;bool rust=false;void* owner=NULL;BYTE* data()const{return rust?base:base+32;}};
static std::vector<CoverageAllocation> coverageAllocations;
static void checkCoverageGuards(const CoverageAllocation& a){
    if(a.rust)return;
    for(size_t i=0;i<32;++i)if(a.base[i]!=0xcc||a.base[a.count+32+i]!=0xcc)std::abort();
}
static void* xy_malloc(size_t count){
    coverageEvents.insert(coverageEvents.end(),{1,static_cast<int64_t>(count)});
    if(allocationAttempt++==allocationFailure)return NULL;
    BYTE* base=static_cast<BYTE*>(std::malloc(count+64));if(!base)std::abort();
    std::memset(base,0xcc,count+64);CoverageAllocation a;a.base=base;a.count=count;coverageAllocations.push_back(a);g_xy_malloc_used_size+=count;return base+32;
}
static void xy_free(void* value){
    if(!value)return;
    for(size_t i=0;i<coverageAllocations.size();++i)if(coverageAllocations[i].data()==value){
        const CoverageAllocation a=coverageAllocations[i];checkCoverageGuards(a);
        coverageEvents.insert(coverageEvents.end(),{2,static_cast<int64_t>(a.count)});
        if(a.rust)std::abort();g_xy_malloc_used_size-=a.count;std::free(a.base);coverageAllocations.erase(coverageAllocations.begin()+i);return;
    }
    std::abort();
}
extern "C" {
void* xy_vsfilter_plane_create(size_t);
uint8_t* xy_vsfilter_plane_loan(void*);
void xy_vsfilter_plane_release(void*);
}
static uint64_t planeOwners=0,planeLoans=0,planeReleases=0;
extern "C" inline void* coveragePlaneCreate(size_t count){
    if(allocationAttempt==allocationFailure)return NULL;
    void* owner=xy_vsfilter_plane_create(count);
    if(owner){
        ++planeOwners;++allocationAttempt;
        coverageEvents.insert(coverageEvents.end(),{1,static_cast<int64_t>(count)});
        CoverageAllocation a;a.base=NULL;a.count=count;a.rust=true;a.owner=owner;coverageAllocations.push_back(a);
    }
    return owner;
}
extern "C" inline uint8_t* coveragePlaneLoan(void* owner){
    uint8_t* data=xy_vsfilter_plane_loan(owner);if(reinterpret_cast<uintptr_t>(data)%16)std::abort();
    for(auto& a:coverageAllocations)if(a.rust&&a.owner==owner){a.base=data;++planeLoans;return data;}
    std::abort();
}
extern "C" inline void coveragePlaneRelease(void* owner){
    for(size_t i=0;i<coverageAllocations.size();++i)if(coverageAllocations[i].rust&&coverageAllocations[i].owner==owner){
        coverageEvents.insert(coverageEvents.end(),{2,static_cast<int64_t>(coverageAllocations[i].count)});
        coverageAllocations.erase(coverageAllocations.begin()+i);++planeReleases;xy_vsfilter_plane_release(owner);return;
    }
    std::abort();
}
#define xy_vsfilter_plane_create coveragePlaneCreate
#define xy_vsfilter_plane_loan coveragePlaneLoan
#define xy_vsfilter_plane_release coveragePlaneRelease
#include "xy_rust_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
extern "C" inline void* memsetSSE2(void* destination,int value,size_t count){
    coverageEvents.insert(coverageEvents.end(),{3,value,static_cast<int64_t>(count)});
    return std::memset(destination,value,count);
}
struct Overlay {
    int mOffsetX,mOffsetY,mWidth,mHeight,mOverlayWidth,mOverlayHeight,mOverlayPitch;
    bool mfWideOutlineEmpty;
    std::shared_ptr<BYTE> mBody,mBorder;
    Overlay():mOffsetX(1),mOffsetY(2),mWidth(3),mHeight(4),mOverlayWidth(5),mOverlayHeight(6),mOverlayPitch(7),mfWideOutlineEmpty(false){}
    void CleanUp(){
        coverageEvents.push_back(4);mBody.reset();mBorder.reset();
        mOffsetX=mOffsetY=mWidth=mHeight=mOverlayWidth=mOverlayHeight=mOverlayPitch=0;mfWideOutlineEmpty=false;
    }
    std::vector<uint64_t> Snapshot()const{
        std::vector<uint64_t> result={static_cast<uint64_t>(mOffsetX),static_cast<uint64_t>(mOffsetY),static_cast<uint64_t>(mWidth),static_cast<uint64_t>(mHeight),static_cast<uint64_t>(mOverlayWidth),static_cast<uint64_t>(mOverlayHeight),static_cast<uint64_t>(mOverlayPitch),mfWideOutlineEmpty?1u:0u};
        for(const auto& plane:{mBody,mBorder}){
            if(!plane){result.push_back(0);continue;}
            bool found=false;
            for(const auto& a:coverageAllocations)if(a.data()==plane.get()){
                checkCoverageGuards(a);result.push_back(a.count);result.insert(result.end(),plane.get(),plane.get()+a.count);found=true;break;
            }
            if(!found)std::abort();
        }
        return result;
    }
};
typedef std::shared_ptr<Overlay> SharedPtrOverlay;
static uint64_t coverageRustSpans=0,coverageFallbacks=0;
namespace XyRust {
inline bool ObservedAccumulateSpan(uint8_t* p,int pitch,int height,int y,int x1,int x2){
    bool ok=AccumulateSpan(p,pitch,height,y,x1,x2);if(ok)++coverageRustSpans;else ++coverageFallbacks;return ok;
}
}
#define COVERAGE_RASTER_CLASS class Rasterizer {typedef BYTE byte;public:static bool Rasterize(const ScanLineData2&,int,int,SharedPtrOverlay);};
