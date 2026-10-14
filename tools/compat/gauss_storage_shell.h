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
#include <windows.h>
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <mmintrin.h>
#else
inline void _mm_empty(){}
#endif
#define ASSERT(x) assert(x)
typedef uint8_t UINT8,*PUINT8;
typedef const uint8_t* PCUINT8;
size_t g_xy_malloc_used_size=0;
static std::vector<uint64_t> gaussEvents,gaussHistory;
static std::vector<bool> gaussWritten;
static uint8_t* gaussData=NULL;
static uint8_t* gaussNativeBase=NULL;
static size_t gaussCount=0;
static void* gaussOwner=NULL;
static uint64_t gaussOwners=0,gaussLoans=0,gaussReleases=0,gaussStages=0;
static bool gaussDecline=false;
static const uint8_t* gaussInput=NULL;
static size_t gaussInputCount=0;
static uint8_t* gaussOutput=NULL;
static size_t gaussOutputCount=0;
static size_t checkedOffset(const void* value,const void* base,size_t length,size_t count){
    const uintptr_t p=reinterpret_cast<uintptr_t>(value),b=reinterpret_cast<uintptr_t>(base);
    if(!base||p<b||p-b>length||count>length-(p-b)){fprintf(stderr,"Gaussian callback outside modeled extent\n");std::abort();}
    return static_cast<size_t>(p-b);
}
static void beginScratch(size_t count){
    if(gaussData||gaussOwner||count==0)std::abort();
    gaussCount=count;gaussWritten.assign(count,false);gaussEvents.insert(gaussEvents.end(),{1,count});
}
static void captureScratch(){
    gaussEvents.push_back(2);
    for(size_t i=0;i<gaussCount;++i)if(gaussWritten[i]){gaussHistory.push_back(i);gaussHistory.push_back(gaussData[i]);}
    // Unwritten scratch bytes are neither read nor compared.
}
static void* xy_malloc(size_t count){
    beginScratch(count);gaussNativeBase=static_cast<uint8_t*>(std::malloc(count+96));if(!gaussNativeBase)std::abort();
    std::memset(gaussNativeBase,0xcc,count+96);gaussData=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(gaussNativeBase+32)+15)&~uintptr_t(15));g_xy_malloc_used_size+=count;return gaussData;
}
static void xy_free(void* p){
    if(p!=gaussData||gaussOwner||!gaussNativeBase)std::abort();captureScratch();
    const size_t offset=static_cast<size_t>(gaussData-gaussNativeBase);
    for(size_t i=0;i<gaussCount+96;++i)if((i<offset||i>=offset+gaussCount)&&gaussNativeBase[i]!=0xcc)std::abort();
    g_xy_malloc_used_size-=gaussCount;std::free(gaussNativeBase);gaussNativeBase=gaussData=NULL;
}
extern "C" {
void* xy_vsfilter_plane_create(size_t);
uint8_t* xy_vsfilter_plane_loan(void*);
void xy_vsfilter_plane_release(void*);
}
extern "C" inline void* gaussPlaneCreate(size_t count){
    if(gaussDecline)return NULL;
    void* owner=xy_vsfilter_plane_create(count);if(owner){beginScratch(count);gaussOwner=owner;++gaussOwners;}return owner;
}
extern "C" inline uint8_t* gaussPlaneLoan(void* owner){
    if(owner!=gaussOwner)std::abort();gaussData=xy_vsfilter_plane_loan(owner);
    if(reinterpret_cast<uintptr_t>(gaussData)%16)std::abort();++gaussLoans;return gaussData;
}
extern "C" inline void gaussPlaneRelease(void* owner){
    if(owner!=gaussOwner)std::abort();captureScratch();xy_vsfilter_plane_release(owner);gaussOwner=NULL;gaussData=NULL;++gaussReleases;
}
#define xy_vsfilter_plane_create gaussPlaneCreate
#define xy_vsfilter_plane_loan gaussPlaneLoan
#define xy_vsfilter_plane_release gaussPlaneRelease
#include "xy_rust_plane.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
static void writeFloat(float* p,float value){
    const size_t offset=checkedOffset(p,gaussData,gaussCount,4);
    std::memcpy(p,&value,4);for(size_t i=0;i<4;++i)gaussWritten[offset+i]=true;
}
static float readFloat(const float* p){
    const size_t offset=checkedOffset(p,gaussData,gaussCount,4);
    for(size_t i=0;i<4;++i)if(!gaussWritten[offset+i]){fprintf(stderr,"Uninitialized modeled Gaussian input\n");std::abort();}
    float value;std::memcpy(&value,p,4);return value;
}
static void stage(unsigned kind,const void* p,int a,int b,int c,int d){
    ++gaussStages;gaussEvents.insert(gaussEvents.end(),{kind,checkedOffset(p,gaussData,gaussCount,0),static_cast<uint64_t>(a),static_cast<uint64_t>(b),static_cast<uint64_t>(c),static_cast<uint64_t>(d)});
}
static void xy_byte_2_float_sse(float* dst,int dw,int ds,PCUINT8 src,int w,int h,int ss){
    stage(3,dst,dw,ds,w,h);gaussEvents.push_back(ss);
    for(int y=0;y<h;++y)for(int x=0;x<dw;++x){
        float value=0;if(x<w){checkedOffset(src+y*ss+x,gaussInput,gaussInputCount,1);value=src[y*ss+x];}
        writeFloat(reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(dst)+y*ds)+x,value);
    }
}
static void xy_filter_sse(float* p,int w,int h,int stride,const float* coefficients,int extent){
    stage(4,p,w,h,stride,extent);uint32_t bits;std::memcpy(&bits,coefficients,4);gaussEvents.push_back(bits);
    for(int y=0;y<h;++y){float* row=reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(p)+y*stride);
        for(int x=-extent;x<w;++x){const float value=x<0?static_cast<float>((x+extent+y)%31):readFloat(row+x)+1;writeFloat(row+x,value);}
    }
}
static void xy_float_2_float_transpose_sse(float* dst,int dw,int ds,const float* src,int w,int h,int ss){
    stage(5,dst,dw,ds,w,h);gaussEvents.push_back(checkedOffset(src,gaussData,gaussCount,0));gaussEvents.push_back(ss);
    for(int y=0;y<w;++y)for(int x=0;x<dw;++x){
        const float value=x<h?readFloat(reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(src)+x*ss)+y):0;
        writeFloat(reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(dst)+y*ds)+x,value);
    }
}
static void xy_float_2_byte_transpose_sse(UINT8* dst,int dw,int ds,const float* src,int w,int h,int ss){
    stage(6,src,dw,ds,w,h);gaussEvents.push_back(ss);
    for(int y=0;y<w;++y)for(int x=0;x<h;++x){
        const float value=readFloat(reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(src)+x*ss)+y);
        checkedOffset(dst+y*ds+x,gaussOutput,gaussOutputCount,1);dst[y*ds+x]=static_cast<uint8_t>(static_cast<unsigned>(value)&255);
    }
}
