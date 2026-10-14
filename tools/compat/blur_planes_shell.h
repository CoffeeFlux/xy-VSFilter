// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "span_coverage_shell.h"
#include <cmath>
#include <cassert>
#define ASSERT(x) assert(x)
#define DEBUG_NEW new
#ifndef _WIN32
using std::max;
#endif
namespace boost {
template<class T> class shared_array {
    std::unique_ptr<T[]> owner;
public:
    explicit shared_array(T* p):owner(p){}
    T* get()const{return owner.get();}
};
}
static uint64_t fixedFilterCalls=0,fractionalFilterCalls=0;
static uint32_t floatBits(float value){uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits;}
static void modeledFilter(BYTE* plane,int w,int h,int pitch,int kind,uint32_t x,uint32_t y){
    bool found=false;
    for(const auto& slot:coverageAllocations)if(slot.data()==plane){if(w<0||h<0||pitch<w||static_cast<size_t>(pitch)*h>slot.count)std::abort();found=true;break;}
    if(!found)std::abort();
    coverageEvents.insert(coverageEvents.end(),{kind,w,h,pitch,x,y});
    for(int row=0;row<h;++row)for(int column=0;column<w;++column)plane[row*pitch+column]=static_cast<BYTE>(plane[row*pitch+column]+kind+row*3+column*7);
}
static void be_blur(BYTE* p,unsigned* unused,int w,int h,int pitch){
    if(!unused)std::abort();++fixedFilterCalls;modeledFilter(p,w,h,pitch,5,0,0);
}
static void xy_be_blur(BYTE* p,int w,int h,int pitch,float x,float y){
    ++fractionalFilterCalls;modeledFilter(p,w,h,pitch,6,floatBits(x),floatBits(y));
}
#define BLUR_PLANES_CLASS class Rasterizer {typedef BYTE byte;public:static bool BeBlur(const Overlay&,float,float,float,SharedPtrOverlay);};
