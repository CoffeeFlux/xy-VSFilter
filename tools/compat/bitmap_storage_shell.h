// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cfenv>
#include <memory>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atltypes.h>
#else
typedef uint8_t BYTE;
typedef uint64_t ULONGLONG;
typedef int32_t HRESULT,LONG;
typedef void* LPVOID;
typedef const void* LPCVOID;
struct POINT {LONG x,y;};
struct SIZE {LONG cx,cy;};
struct CRect {LONG left,top,right,bottom;CRect(LONG l=0,LONG t=0,LONG r=0,LONG b=0):left(l),top(t),right(r),bottom(b){}LONG Width()const{return right-left;}LONG Height()const{return bottom-top;}};
#define S_OK HRESULT(0)
#define E_INVALIDARG HRESULT(-2147024809)
#define STDMETHODIMP HRESULT
#endif
struct SubPicDesc;
static uint64_t bitmapAssertions=0,bitmapOwners=0,bitmapLoans=0,bitmapReleases=0,bitmapDeclines=0;
#define ASSERT(x) do{if(!(x))++bitmapAssertions;}while(0)
#define TRACE_SUB_RENDER_FRAME(x) ((void)0)
static void bitmapRequire(bool value){if(!value){fprintf(stderr,"Bitmap storage observation failed\n");std::abort();}}
size_t g_xy_malloc_used_size=0;
struct BitmapRegion {uint8_t* data;uint8_t* raw;void* owner;size_t count;unsigned shift;std::vector<bool> initialized;};
static std::vector<BitmapRegion> bitmapRegions;
static std::vector<uint64_t> bitmapEvents;
static bool bitmapDecline=false,bitmapObjectFail=false;
static size_t bitmapRequestedCount=0;
static unsigned bitmapRequestedShift=0;
static BitmapRegion& bitmapBase(const void* p){for(auto& region:bitmapRegions)if(region.data==p)return region;bitmapRequire(false);return bitmapRegions[0];}
static BitmapRegion& bitmapRange(const void* p,size_t count,size_t& offset){
    const uintptr_t address=reinterpret_cast<uintptr_t>(p);
    for(auto& region:bitmapRegions){const uintptr_t base=reinterpret_cast<uintptr_t>(region.data);if(address>=base&&address-base<=region.count&&count<=region.count-(address-base)){offset=address-base;return region;}}
    bitmapRequire(false);return bitmapRegions[0];
}
static void bitmapGuards(const BitmapRegion& region){if(region.raw){for(size_t i=0;i<16;++i)bitmapRequire(region.raw[i]==0xab&&region.data[region.count+i]==0xab);}}
static void* xy_malloc(size_t count,int shift){
    bitmapRequire(count<100000);shift&=15;
    auto* raw=new uint8_t[count+96];std::memset(raw,0xab,count+96);
    auto* data=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(raw+32)+15)&~uintptr_t(15))+shift;
    bitmapRegions.push_back({data,raw,NULL,count,static_cast<unsigned>(shift),std::vector<bool>(count,false)});
    bitmapEvents.insert(bitmapEvents.end(),{1,count,static_cast<unsigned>(shift)});g_xy_malloc_used_size+=count;return data;
}
static void xy_free(void* p){
    if(!p){bitmapEvents.push_back(5);return;}
    for(size_t i=0;i<bitmapRegions.size();++i)if(bitmapRegions[i].data==p){const auto region=bitmapRegions[i];bitmapRequire(region.raw&&!region.owner);bitmapGuards(region);bitmapEvents.insert(bitmapEvents.end(),{2,region.count,region.shift});g_xy_malloc_used_size-=region.count;delete[] region.raw;bitmapRegions.erase(bitmapRegions.begin()+i);return;}
    bitmapRequire(false);
}
#include "xy_rust_plane.h"
static void* bitmapCreate(size_t count){
    bitmapRequire(count==bitmapRequestedCount+bitmapRequestedShift);
    if(bitmapDecline){++bitmapDeclines;return NULL;}
    void* owner=xy_vsfilter_plane_create(count);bitmapRequire(owner!=NULL);
    bitmapRegions.push_back({NULL,NULL,owner,bitmapRequestedCount,bitmapRequestedShift,std::vector<bool>(bitmapRequestedCount,false)});++bitmapOwners;return owner;
}
static uint8_t* bitmapLoan(void* owner){
    auto* base=xy_vsfilter_plane_loan(owner);bitmapRequire(reinterpret_cast<uintptr_t>(base)%16==0);
    for(auto& region:bitmapRegions)if(region.owner==owner){bitmapRequire(!region.data);region.data=base+region.shift;bitmapEvents.insert(bitmapEvents.end(),{1,region.count,region.shift});++bitmapLoans;return base;}
    bitmapRequire(false);return NULL;
}
static void bitmapRelease(void* owner){
    if(!owner){xy_vsfilter_plane_release(owner);return;}
    for(size_t i=0;i<bitmapRegions.size();++i)if(bitmapRegions[i].owner==owner){const auto& region=bitmapRegions[i];bitmapEvents.insert(bitmapEvents.end(),{2,region.count,region.shift});bitmapRegions.erase(bitmapRegions.begin()+i);xy_vsfilter_plane_release(owner);++bitmapReleases;return;}
    bitmapRequire(false);
}
#define xy_vsfilter_plane_create bitmapCreate
#define xy_vsfilter_plane_loan bitmapLoan
#define xy_vsfilter_plane_release bitmapRelease
#include "xy_rust_shifted_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
namespace XyRust {
class ObservedShiftedPlaneStorage:public ShiftedPlaneStorage {public:
    bool Allocate(size_t count,unsigned shift){bitmapRequestedCount=count;bitmapRequestedShift=shift&15;return ShiftedPlaneStorage::Allocate(count,shift);}
};
}
template<class T>static T* bitmapNew(){bitmapEvents.push_back(6);return bitmapObjectFail?NULL:new T();}
static void bitmapSet(void* p,int value,size_t count){
    size_t offset=0;auto& region=bitmapRange(p,count,offset);bitmapEvents.insert(bitmapEvents.end(),{3,offset,count,static_cast<unsigned>(value)});
    std::memset(p,value,count);for(size_t i=0;i<count;++i)region.initialized[offset+i]=true;
}
static void bitmapSetWords(void* p,unsigned value,size_t count){
    bitmapRequire(count%4==0);size_t offset=0;auto& region=bitmapRange(p,count,offset);bitmapEvents.insert(bitmapEvents.end(),{4,offset,count,value});
    for(size_t i=0;i<count;i+=4)std::memcpy(static_cast<uint8_t*>(p)+i,&value,4);
    for(size_t i=0;i<count;++i)region.initialized[offset+i]=true;
}
template<class T>struct BitmapArray {std::vector<T> values;size_t GetCount()const{return values.size();}const T& GetAt(size_t i)const{return values.at(i);}};
#define BITMAP_FRAME_CLASS class XySubRenderFrame {public: \
    BitmapArray<std::shared_ptr<XyBitmap>> m_bitmaps;BitmapArray<int> m_bitmap_ids;CRect m_output_rect;XyColorSpace m_xy_color_space; \
    HRESULT GetBitmap(int,ULONGLONG*,POINT*,SIZE*,LPCVOID*,int*);HRESULT GetBitmapExtra(int,LPVOID); \
};
