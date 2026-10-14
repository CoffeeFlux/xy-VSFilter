// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>
#include <algorithm>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <atlbase.h>
#include <atltypes.h>
#else
struct CPoint {int32_t x,y;CPoint(int32_t a=0,int32_t b=0):x(a),y(b){}bool operator!=(const CPoint& o)const{return x!=o.x||y!=o.y;}};
typedef CPoint POINT;typedef uint8_t BYTE;
struct CSize {int32_t cx,cy;CSize(int32_t x,int32_t y):cx(x),cy(y){}};
#define PT_CLOSEFIGURE 1
#define PT_LINETO 2
#define PT_BEZIERTO 4
#define PT_MOVETO 6
#endif
#include "xy_rust_edges.h"
#define PT_MOVETONC 0xfe
#define PT_BSPLINETO 0xfc
#define PT_BSPLINEPATCHTO 0xfa
#define DEBUG_NEW new
struct PathData {
    std::vector<POINT> points;std::vector<BYTE> types;int mPathPoints;POINT* mpPathPoints;BYTE* mpPathTypes;
    explicit PathData(const std::vector<BYTE>& t):types(t),mPathPoints(static_cast<int>(t.size())){
        for(size_t i=0;i<t.size();++i){POINT p={static_cast<int32_t>(i*7%31),static_cast<int32_t>(i*11%23)};points.push_back(p);}mpPathPoints=points.data();mpPathTypes=types.data();
    }
};
static uint64_t edgeOwners=0,edgeFallbacks=0,edgeWrites=0,edgeGrowths=0,edgeReads=0,edgeResets=0,scanRows=0,scanRowFallbacks=0;
namespace XyRust {
class ObservedEdgeStorage:public EdgeStorage {
public:
    bool Initialize(int h,unsigned c){bool ok=EdgeStorage::Initialize(h,c);if(ok)++edgeOwners;else ++edgeFallbacks;return ok;}
    void Insert(unsigned i,int row,int p){++edgeWrites;EdgeStorage::Insert(i,row,p);}
    void Resize(unsigned c){++edgeGrowths;EdgeStorage::Resize(c);}
    unsigned Head(size_t row)const{++edgeReads;return EdgeStorage::Head(row);}
    unsigned Next(unsigned i)const{++edgeReads;return EdgeStorage::Next(i);}
    int Position(unsigned i)const{++edgeReads;return EdgeStorage::Position(i);}
    void Reset(){if(Ready())++edgeResets;EdgeStorage::Reset();}
    template<class Spans> bool AppendRow(int row,Spans& destination)const{bool ok=EdgeStorage::AppendRow(row,destination);if(ok)++scanRows;else ++scanRowFallbacks;return ok;}
};
}
#define EDGE_SCAN_CLASS \
class ScanLineData { \
public: \
    struct Edge{int next,posandflag;}; \
    bool fFirstSet;CPoint firstp,lastp;int mWidth,mHeight;std::vector<std::pair<uint64_t,uint64_t>> mOutline; \
    Edge* mpEdgeBuffer;unsigned mEdgeHeapSize,mEdgeNext;unsigned* mpScanBuffer;EDGE_OWNER \
    std::vector<int32_t> preset;bool presetMode=false,emitted=false;std::vector<uint64_t> trace,records;unsigned budget;uint32_t random; \
    explicit ScanLineData(unsigned n,uint32_t seed):fFirstSet(true),firstp(7,8),lastp(9,10),mWidth(11),mHeight(12),mpEdgeBuffer(NULL),mEdgeHeapSize(0),mEdgeNext(0),mpScanBuffer(NULL),budget(n),random(seed){mOutline.reserve(13);mOutline.push_back({71,72});} \
    void _ReallocEdgeBuffer(unsigned);void _AddEdge(int,int);bool ScanConvert(const PathData&,const CSize&); \
    uint32_t NextRandom(){random^=random<<13;random^=random>>17;random^=random<<5;return random;} \
    void EmitOne(unsigned row,int position){if(mEdgeNext>=mEdgeHeapSize){trace.push_back(200);trace.push_back(mEdgeHeapSize*2);_ReallocEdgeBuffer(mEdgeHeapSize*2);}_AddEdge(static_cast<int>(row),position);} \
    void Emit(){if(mHeight<=0)return;if(presetMode){if(!emitted){for(int32_t p:preset)EmitOne(budget%static_cast<unsigned>(mHeight),p);emitted=true;}return;}for(unsigned i=0;i<budget;++i){if(mEdgeNext>=mEdgeHeapSize){trace.push_back(200);trace.push_back(mEdgeHeapSize*2);_ReallocEdgeBuffer(mEdgeHeapSize*2);}unsigned row=NextRandom()%static_cast<unsigned>(mHeight);int position=static_cast<int32_t>(NextRandom());if(mEdgeNext%4==1)position=1;if(mEdgeNext%4==2)position=4;_AddEdge(static_cast<int>(row),position);}} \
    void _EvaluateLine(int x0,int y0,int x1,int y1){trace.insert(trace.end(),{100,static_cast<uint64_t>(x0),static_cast<uint64_t>(y0),static_cast<uint64_t>(x1),static_cast<uint64_t>(y1)});if(!fFirstSet){firstp=CPoint(x0,y0);fFirstSet=true;}lastp=CPoint(x1,y1);Emit();} \
    void _EvaluateLine(const PathData& p,int a,int b){if(a<0||b<0||a>=p.mPathPoints||b>=p.mPathPoints)std::abort();trace.insert(trace.end(),{101,static_cast<uint64_t>(a),static_cast<uint64_t>(b)});_EvaluateLine(p.mpPathPoints[a].x,p.mpPathPoints[a].y,p.mpPathPoints[b].x,p.mpPathPoints[b].y);} \
    void _EvaluateBezier(const PathData& p,int base,bool spline){if(base<0||base+3>=p.mPathPoints)std::abort();trace.insert(trace.end(),{102,static_cast<uint64_t>(base),spline?1u:0u});_EvaluateLine(p.mpPathPoints[base].x,p.mpPathPoints[base].y,p.mpPathPoints[base+3].x,p.mpPathPoints[base+3].y);} \
    void Checkpoint(){EDGE_CHECK records.push_back(mEdgeHeapSize);records.push_back(mEdgeNext);for(int row=0;row<mHeight;++row)records.push_back(EDGE_HEAD(row));for(unsigned i=1;i<mEdgeNext;++i){records.push_back(EDGE_NEXT(i));records.push_back(static_cast<uint32_t>(EDGE_POS(i)));}} \
    std::vector<uint64_t> Snapshot()const{std::vector<uint64_t> out={static_cast<uint64_t>(mWidth),static_cast<uint64_t>(mHeight),fFirstSet?1u:0u,static_cast<uint64_t>(firstp.x),static_cast<uint64_t>(firstp.y),static_cast<uint64_t>(lastp.x),static_cast<uint64_t>(lastp.y),mEdgeHeapSize,mEdgeNext,mOutline.size(),mOutline.capacity(),random};for(const auto& span:mOutline){out.push_back(span.first);out.push_back(span.second);}return out;} \
};
