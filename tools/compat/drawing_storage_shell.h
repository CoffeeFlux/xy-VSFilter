// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "drawing_shell.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <cfenv>
#include <cerrno>
using std::max;
typedef CPoint POINT;
#define TRACE_PARSER(message) ((void)0)
static std::map<void*,size_t> drawingAllocations;
static std::vector<size_t> drawingAllocationEvents;
static int drawingAllocationFailure=0,drawingAllocationCalls=0;
static void checkDrawingGuard(void* data){
    if(!data)return;
    auto found=drawingAllocations.find(data);if(found==drawingAllocations.end())std::abort();
    unsigned char* begin=static_cast<unsigned char*>(data)-16;
    for(unsigned i=0;i<16;++i)if(begin[i]!=0xcc || begin[16+found->second+i]!=0xcc)std::abort();
}
static void freeDrawingAllocation(void* data){if(data){checkDrawingGuard(data);drawingAllocations.erase(data);std::free(static_cast<unsigned char*>(data)-16);}}
static void* drawingRealloc(void* data,size_t size){
    checkDrawingGuard(data);size_t previous=data?drawingAllocations.at(data):0;
    bool failure=++drawingAllocationCalls==drawingAllocationFailure;
    drawingAllocationEvents.insert(drawingAllocationEvents.end(),{previous,size,failure?1u:0u});
    if(failure)return NULL;
    unsigned char* block=static_cast<unsigned char*>(std::malloc(size+32));if(!block)std::abort();
    std::memset(block,0xcc,size+32);std::memset(block+16,0xa5,size);
    if(data)std::memcpy(block+16,data,(std::min)(previous,size));freeDrawingAllocation(data);
    drawingAllocations[block+16]=size;return block+16;
}
struct PathData {
    int mPathPoints;BYTE* mpPathTypes;POINT* mpPathPoints;
    explicit PathData(int count):mPathPoints(count),mpPathTypes(NULL),mpPathPoints(NULL){
        if(count){mpPathTypes=static_cast<BYTE*>(drawingRealloc(NULL,count));mpPathPoints=static_cast<POINT*>(drawingRealloc(NULL,count*sizeof(POINT)));}
    }
    ~PathData(){freeDrawingAllocation(mpPathTypes);freeDrawingAllocation(mpPathPoints);}
    std::vector<uint64_t> snapshot()const{
        std::vector<uint64_t> out={static_cast<uint64_t>(mPathPoints)};
        for(void* data:{static_cast<void*>(mpPathTypes),static_cast<void*>(mpPathPoints)}){
            checkDrawingGuard(data);size_t length=data?drawingAllocations.at(data):0;out.push_back(data?1:0);out.push_back(length);
            const BYTE* bytes=static_cast<const BYTE*>(data);for(size_t i=0;i<length;++i)out.push_back(bytes[i]);
        }
        return out;
    }
};
struct StoredStyle {double fontScaleX,fontScaleY;};
struct StoredStyleRef {StoredStyle style;const StoredStyle& get()const{return style;}};
struct CWord {
    CStringW m_str;StoredStyleRef m_style;int m_width,m_ascent,m_descent;
    explicit CWord(const std::vector<DrawingToken>& tokens):m_str(tokens),m_width(71),m_ascent(72),m_descent(73){m_style.style={100,100};}
};
static uint64_t storedCopies=0,storedSwaps=0,storedPointUpdates=0,storedClears=0;
namespace XyRust {
class ObservedStoredDrawing : public DrawingPath {
public:
    void Copy(const DrawingPath& source){++storedCopies;DrawingPath::Copy(source);}
    void Swap(DrawingPath& other){++storedSwaps;DrawingPath::Swap(other);}
    void SetPoint(size_t index,int32_t x,int32_t y){++storedPointUpdates;DrawingPath::SetPoint(index,x,y);}
    void Clear(){++storedClears;DrawingPath::Clear();}
};
}
#define STORED_DRAWING_SHELL(Name) \
class Name : public CWord { \
public: \
    double m_scalex,m_scaley;int m_baseline; \
    DrawingArray<BYTE> m_pathTypesOrg;DrawingArray<CPoint> m_pathPointsOrg; \
    XyRust::ObservedStoredDrawing m_pathOrg;std::vector<uint64_t> requests; \
    explicit Name(const std::vector<DrawingToken>& input):CWord(input),m_scalex(1),m_scaley(1),m_baseline(0){} \
    Name(Name& source); \
    bool GetPOINT(CStringW& token,CPoint& point){ \
        requests.push_back((static_cast<uint64_t>(token.token.command)<<32)|token.point); \
        if(token.point>=token.token.points.size())return false; \
        const PointResult& result=token.token.points[token.point++];if(result.success)point=result.point;return result.success; \
    } \
    bool ParseStr();bool CreatePath(PathData*); \
};
