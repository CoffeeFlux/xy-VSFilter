// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ssf_stream_shell.h"
#include <map>
#include <cstring>
#ifndef _WIN32
typedef unsigned char BYTE;
#endif
struct SsfMemoryAllocation {BYTE* raw;size_t size;};
struct SsfMemoryContext {
    std::map<BYTE*,SsfMemoryAllocation> allocations;
    std::vector<int64_t> events;
    bool decline=false;
};
static SsfMemoryContext* ssfMemoryContext=NULL;
static uint64_t ssfMemoryOwners=0,ssfMemoryReleases=0,ssfMemoryReads=0,ssfMemoryDeclines=0;
static BYTE* ssfMemoryNew(int size){
    ssfRequire(size>=0);BYTE* raw=static_cast<BYTE*>(std::malloc(static_cast<size_t>(size)+32));ssfRequire(raw!=NULL);
    std::memset(raw,0xa5,static_cast<size_t>(size)+32);BYTE* data=raw+16;
    ssfMemoryContext->allocations[data]=SsfMemoryAllocation{raw,static_cast<size_t>(size)};
    ssfMemoryContext->events.insert(ssfMemoryContext->events.end(),{1,size});return data;
}
static void ssfMemoryDelete(BYTE* data){
    if(!data)return;
    auto it=ssfMemoryContext->allocations.find(data);ssfRequire(it!=ssfMemoryContext->allocations.end());
    auto allocation=it->second;
    for(size_t i=0;i<16;++i)ssfRequire(allocation.raw[i]==0xa5&&data[allocation.size+i]==0xa5);
    ssfMemoryContext->events.insert(ssfMemoryContext->events.end(),{2,static_cast<int64_t>(allocation.size)});
    std::free(allocation.raw);ssfMemoryContext->allocations.erase(it);
}
static void ssfMemoryCopy(BYTE* destination,const BYTE* source,size_t count){
    const auto a=ssfMemoryContext->allocations.find(destination),b=ssfMemoryContext->allocations.find(const_cast<BYTE*>(source));
    ssfRequire(a!=ssfMemoryContext->allocations.end()&&b!=ssfMemoryContext->allocations.end()&&count<=a->second.size&&count<=b->second.size);
    std::memcpy(destination,source,count);
}
namespace XyRust {
class ObservedSsfMemory:public SsfMemory {
    int length=0;
public:
    ~ObservedSsfMemory(){Reset();}
    bool Assign(const uint8_t* input,int size){
        if(size>0&&ssfMemoryContext->decline){++ssfMemoryDeclines;return false;}
        if(!SsfMemory::Assign(input,size))return false;
        length=size;++ssfMemoryOwners;ssfMemoryContext->events.insert(ssfMemoryContext->events.end(),{1,size});return true;
    }
    int Read(int position)const{++ssfMemoryReads;return SsfMemory::Read(position);}
    void Reset(){if(Ready()){++ssfMemoryReleases;ssfMemoryContext->events.insert(ssfMemoryContext->events.end(),{2,length});}SsfMemory::Reset();}
};
}
