// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "xy_rust_vobsub.h"
struct CPoint {
    int32_t x,y;CPoint(int32_t a=0,int32_t b=0):x(a),y(b){}
    CPoint operator+(const CPoint& other)const{return CPoint(x+other.x,y+other.y);}
};
template<class T> struct SegmentArray {
    std::vector<T> values;
    size_t GetCount()const{return values.size();}
    const T* GetData()const{return values.data();}
    const T& operator[](size_t index)const{if(index>=values.size()){fprintf(stderr,"Segment fixture left its allocated array\n");std::abort();}return values[index];}
};
struct SegmentInterrupted {};
struct COutline;
static COutline *segmentSource=nullptr,*segmentOutput=nullptr;
static std::vector<int64_t> segmentEvents;
static int segmentFailure=-1,segmentAttempts=0,segmentAssertions=0;
static bool segmentMutate=false,segmentMutated=false;
static uint64_t segmentHandled=0,segmentFallback=0;
static void observeSegment(int action,CPoint point=CPoint(),int direction=0);
struct COutline {
    SegmentArray<CPoint> pa;SegmentArray<int> da;
    void RemoveAll(){pa.values.clear();observeSegment(1);da.values.clear();observeSegment(2);}
    void Add(CPoint point,int direction){
        observeSegment(3,point,direction);if(segmentAttempts++==segmentFailure)throw SegmentInterrupted();
        pa.values.push_back(point);observeSegment(4,point,direction);if(segmentAttempts++==segmentFailure)throw SegmentInterrupted();
        da.values.push_back(direction);observeSegment(5,point,direction);
        if(segmentMutate && !segmentMutated){
            segmentSource->pa.values.back().x+=7;segmentSource->da.values.back()=-3;
            segmentSource->pa.values.push_back(CPoint(123,456));segmentSource->da.values.push_back(7);
            segmentMutated=true;observeSegment(6);
        }
    }
};
static std::vector<int64_t> segmentState(const COutline& o) {
    std::vector<int64_t> result={int64_t(o.pa.GetCount()),int64_t(o.da.GetCount())};
    for(const auto& p:o.pa.values){result.push_back(p.x);result.push_back(p.y);}for(int d:o.da.values)result.push_back(d);return result;
}
static void observeSegment(int action,CPoint point,int direction) {
    segmentEvents.insert(segmentEvents.end(),{action,point.x,point.y,direction});
    for(auto* o:{segmentSource,segmentOutput}){const auto state=segmentState(*o);segmentEvents.insert(segmentEvents.end(),state.begin(),state.end());}
}
#define ASSERT(condition) do{if(!(condition)){++segmentAssertions;observeSegment(7);}}while(0)
#define VOBSUB_SEGMENT_SHELL(Name) \
class Name {public:int GrabSegment(int,const COutline&,COutline&);};
static uint32_t observedSegment(const int32_t* points,size_t pointCount,const int32_t* directions,size_t directionCount,int32_t start,XyVobSubSegment* output) {
    const auto result=xy_vsfilter_vobsub_segment(points,pointCount,directions,directionCount,start,output);segmentHandled+=result!=0;segmentFallback+=result==0;return result;
}
#define xy_vsfilter_vobsub_segment observedSegment
