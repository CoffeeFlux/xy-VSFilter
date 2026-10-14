// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include "xy_rust_vobsub.h"
typedef uint32_t DWORD;
struct CRect {
    int32_t left,top,right,bottom;
    CRect(int32_t l,int32_t t,int32_t r,int32_t b):left(l),top(t),right(r),bottom(b){}
    CRect():left(0),top(0),right(0),bottom(0){}
    int32_t Width()const{return right-left;}
    int32_t Height()const{return bottom-top;}
};
struct ScaleAllocation {};
struct ScaleInterrupted {};
static int scaleFailure=-1;
static std::vector<size_t> scaleAllocations;
static uint64_t scaleHandled=0,scaleFallback=0;
inline void* operator new[](size_t bytes,ScaleAllocation) {
    scaleAllocations.push_back(bytes);
    if(static_cast<int>(scaleAllocations.size())-1==scaleFailure)throw ScaleInterrupted();
    return ::operator new[](bytes);
}
inline void operator delete[](void* memory,ScaleAllocation) noexcept {::operator delete[](memory);}
#define DEBUG_NEW new (ScaleAllocation{})
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define VOBSUB_SCALE_SHELL(Name) \
class Name { public: CRect rect;DWORD* lpPixels;void Scale2x(); };
static uint32_t observedScale(const uint32_t* source,uint32_t* destination,int32_t width,int32_t height) {
    const auto result=xy_vsfilter_vobsub_scale(source,destination,width,height);scaleHandled+=result!=0;scaleFallback+=result==0;return result;
}
#define xy_vsfilter_vobsub_scale observedScale
