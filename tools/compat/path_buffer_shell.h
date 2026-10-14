// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
typedef long PathOffset;
#else
struct POINT {int32_t x,y;};typedef uint8_t BYTE;typedef void* HDC;typedef int32_t PathOffset;
#endif
#include "xy_rust_path_buffer.h"
#define DEBUG_NEW new
struct PathContext {
    bool closeResult,endResult;int count,fillMode,breaks;
    std::vector<POINT> points;std::vector<BYTE> types;std::vector<uint64_t> events;
    explicit PathContext(int n):closeResult(true),endResult(true),count(n),fillMode(0),breaks(0){
        for(int i=0;i<n;++i){POINT p={i*73-125,199-i*91};points.push_back(p);types.push_back(static_cast<BYTE>(i*47));}
    }
};
static PathContext* pathContext=NULL;
static int livePathBuffers=0;
static uint64_t pathRustOwners=0,pathRustLoans=0,pathRustReleases=0,pathRustFallbacks=0;
static std::map<void*,size_t> pathAllocations;
static std::vector<uint64_t> pathAllocationEvents;
static int pathAllocationFailure=0,pathAllocationCalls=0;
static void pathGuard(void* data){
    if(!data)return;auto it=pathAllocations.find(data);if(it==pathAllocations.end())std::abort();
    auto p=static_cast<BYTE*>(data)-16;for(unsigned i=0;i<16;++i)if(p[i]!=0xcc||p[16+it->second+i]!=0xcc)std::abort();
}
static void pathFree(void* data){if(data){pathGuard(data);pathAllocations.erase(data);std::free(static_cast<BYTE*>(data)-16);}}
static void* pathRealloc(void* data,size_t size){
    pathGuard(data);size_t old=data?pathAllocations.at(data):0;bool failed=++pathAllocationCalls==pathAllocationFailure;
    pathAllocationEvents.insert(pathAllocationEvents.end(),{old,size,failed?1u:0u});if(failed)return NULL;
    auto block=static_cast<BYTE*>(std::malloc(size+32));if(!block)std::abort();std::memset(block,0xcc,size+32);std::memset(block+16,0xa5,size);
    if(data)std::memcpy(block+16,data,(std::min)(old,size));pathFree(data);pathAllocations[block+16]=size;return block+16;
}
struct PathBufferState {
    BYTE* mpPathTypes;POINT* mpPathPoints;int mPathPoints;
    explicit PathBufferState(int n):mpPathTypes(NULL),mpPathPoints(NULL),mPathPoints(n){
        if(n){mpPathTypes=static_cast<BYTE*>(pathRealloc(NULL,n));mpPathPoints=static_cast<POINT*>(pathRealloc(NULL,n*sizeof(POINT)));}
    }
    ~PathBufferState(){pathFree(mpPathTypes);pathFree(mpPathPoints);}
    std::vector<uint64_t> snapshot()const{
        std::vector<uint64_t> out={static_cast<uint64_t>(mPathPoints)};
        for(void* data:{static_cast<void*>(mpPathTypes),static_cast<void*>(mpPathPoints)}){pathGuard(data);size_t n=data?pathAllocations.at(data):0;out.push_back(data?1:0);out.push_back(n);auto bytes=static_cast<BYTE*>(data);for(size_t i=0;i<n;++i)out.push_back(bytes[i]);}return out;
    }
};
static void PathTemporaryCreated(){++livePathBuffers;pathContext->events.push_back(50);}
static void PathTemporaryReleased(){--livePathBuffers;pathContext->events.push_back(51);}
static int PathCloseFigure(HDC){pathContext->events.push_back(1);return pathContext->closeResult;}
static int PathEndPath(HDC){pathContext->events.push_back(2);return pathContext->endResult;}
static int PathAbortPath(HDC){pathContext->events.push_back(3);if(livePathBuffers)std::abort();return 1;}
static void PathDebugBreak(){pathContext->events.push_back(4);++pathContext->breaks;}
static int PathGetPath(HDC,POINT* points,BYTE* types,int count){
    pathContext->events.insert(pathContext->events.end(),{5,static_cast<uint64_t>(count),points?1u:0u,types?1u:0u});
    if(!points&&!types&&count==0)return pathContext->count;
    if(!points||!types||count!=pathContext->count||livePathBuffers!=1)std::abort();
    int written=pathContext->fillMode==0||pathContext->fillMode==3?count:(pathContext->fillMode==1?count/2:0);
    for(int i=0;i<written;++i){points[i]=pathContext->points[i];types[i]=pathContext->types[i];}
    return pathContext->fillMode==2?-1:(pathContext->fillMode==3?count+1:written);
}
namespace XyRust {
class ObservedPathBuffer:public PathBuffer {
public:
    explicit ObservedPathBuffer(int n):PathBuffer(n){if(Ready()){++pathRustOwners;PathTemporaryCreated();}else ++pathRustFallbacks;}
    template<class Point,class Byte>void Loan(Point*& points,Byte*& types){++pathRustLoans;PathBuffer::Loan(points,types);}
    void Release(){if(Ready()){++pathRustReleases;PathBuffer::Release();PathTemporaryReleased();}}
};
}
