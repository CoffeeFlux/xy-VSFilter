// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "xy_rust_vobsub.h"
typedef uint32_t UINT;
struct CPoint {
    int32_t x,y;CPoint(int32_t a=0,int32_t b=0):x(a),y(b){}
    CPoint operator+(const CPoint& p)const{return CPoint(x+p.x,y+p.y);}
};
struct SplitInterrupted {};
struct COutline;
static COutline *splitSource=nullptr,*splitFirst=nullptr,*splitSecond=nullptr;
static std::vector<int64_t> splitEvents;
static int splitFailure=-1,splitAttempts=0,splitAssertions=0,splitTableId=0;
static bool splitMutate=false,splitMutated=false;
static uint64_t splitHandled=0,splitFallback=0;
static void observeSplit(int action,int object=0,int64_t a=0,int64_t b=0,int64_t c=0);
template<class T> struct SplitArray {
    std::vector<T> values;
    size_t GetCount()const{return values.size();}
    const T* GetData()const{return values.data();}
    const T& operator[](size_t i)const{if(i>=values.size()){fprintf(stderr,"Split fixture left its allocated array\n");std::abort();}return values[i];}
};
template<class T> struct CAtlArray:SplitArray<T> {
    int id;CAtlArray():id(++splitTableId){}
    void Add(T value){observeSplit(10,id,value);if(splitAttempts++==splitFailure)throw SplitInterrupted();this->values.push_back(value);observeSplit(11,id,value);}
};
struct COutline {
    SplitArray<CPoint> pa;SplitArray<int> da;
    int id;
    explicit COutline(int value=0):id(value){}
    void RemoveAll(){pa.values.clear();observeSplit(1,id);da.values.clear();observeSplit(2,id);}
    void Add(CPoint point,int direction){
        observeSplit(3,id,point.x,point.y,direction);if(splitAttempts++==splitFailure)throw SplitInterrupted();
        pa.values.push_back(point);observeSplit(4,id,point.x,point.y,direction);if(splitAttempts++==splitFailure)throw SplitInterrupted();
        da.values.push_back(direction);observeSplit(5,id,point.x,point.y,direction);
        if(splitMutate && !splitMutated){
            for(auto& p:splitSource->pa.values){p.x+=7;p.y-=3;}
            for(auto& d:splitSource->da.values)d=-9;
            splitSource->pa.values.push_back(CPoint(123,456));splitSource->da.values.push_back(7);
            splitMutated=true;observeSplit(6,id);
        }
    }
};
static std::vector<int64_t> splitState(const COutline& o){
    std::vector<int64_t> result={int64_t(o.pa.GetCount()),int64_t(o.da.GetCount())};
    for(auto p:o.pa.values){result.push_back(p.x);result.push_back(p.y);}for(int d:o.da.values)result.push_back(d);return result;
}
static void observeSplit(int action,int object,int64_t a,int64_t b,int64_t c){
    splitEvents.insert(splitEvents.end(),{action,object,a,b,c});
    for(auto* o:{splitSource,splitFirst,splitSecond}){auto state=splitState(*o);splitEvents.insert(splitEvents.end(),state.begin(),state.end());}
}
#define ASSERT(condition) do{if(!(condition)){++splitAssertions;observeSplit(7);}}while(0)
#define VOBSUB_SPLIT_SHELL(Name) class Name{public:void SplitOutline(const COutline&,COutline&,COutline&);};
static uint32_t observedSplit(const uint32_t* lengths,const uint32_t* starts,const uint32_t* ends,size_t count,size_t* output){
    auto result=xy_vsfilter_vobsub_split(lengths,starts,ends,count,output);splitHandled+=result!=0;splitFallback+=result==0;return result;
}
#define xy_vsfilter_vobsub_split observedSplit
