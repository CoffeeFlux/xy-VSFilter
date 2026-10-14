// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cerrno>
#include <cfenv>
#include "xy_rust_path_align.h"
#ifdef _WIN32
#include <atlbase.h>
#include <atltypes.h>
#else
struct POINT {int32_t x,y;};
struct CPoint : POINT {CPoint(int32_t a=0,int32_t b=0){x=a;y=b;}};
struct CSize {int32_t cx,cy;CSize(int32_t a=0,int32_t b=0):cx(a),cy(b){}};
#endif
struct AlignState {
    std::vector<POINT> storage;std::vector<unsigned char> types;
    POINT* mpPathPoints;unsigned char* mpPathTypes;int mPathPoints;unsigned clears;
    AlignState(const std::vector<POINT>& input,int count):storage(input.size()+4),types(input.size()+8,0xa5),mpPathPoints(NULL),mpPathTypes(NULL),mPathPoints(count),clears(0){
        for(auto& point:storage){point.x=0x12345678;point.y=-7654321;}
        for(size_t i=0;i<input.size();++i)storage[i+2]=input[i];
        mpPathPoints=storage.data()+2;mpPathTypes=types.data()+4;
    }
    void _TrashPath(){mpPathPoints=NULL;mpPathTypes=NULL;mPathPoints=0;++clears;}
    std::vector<int64_t> snapshot()const{
        for(size_t i=0;i<storage.size();++i)if(i<2 || i>=storage.size()-2)if(storage[i].x!=0x12345678 || storage[i].y!=-7654321)std::abort();
        for(unsigned char type:types)if(type!=0xa5)std::abort();
        std::vector<int64_t> out={mPathPoints,clears,mpPathPoints?1:0,mpPathTypes?1:0};
        for(const auto& p:storage){out.push_back(p.x);out.push_back(p.y);}for(unsigned char type:types)out.push_back(type);return out;
    }
};
static uint64_t alignHandled=0,alignFallbacks=0;
namespace XyRust {
template<class Point> bool ObservedAlignPath(Point* points,int count,int32_t (&geometry)[4]){bool ok=AlignPath(points,count,geometry);if(ok)++alignHandled;else ++alignFallbacks;return ok;}
}
