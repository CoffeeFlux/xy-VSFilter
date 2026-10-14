// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_drawing_storage.h"
static uint64_t comparisons=0,vertices=0,copies=0,handoffs=0,failedAllocations=0,bytes=0,fpFlags=0;
static std::vector<int64_t> snapshot(OriginalStoredDrawing& path){
    std::vector<int64_t> out={path.m_width,path.m_ascent,path.m_descent,static_cast<int64_t>(path.m_pathTypesOrg.GetCount())};
    for(size_t i=0;i<path.m_pathTypesOrg.GetCount();++i)out.insert(out.end(),{path.m_pathTypesOrg[i],path.m_pathPointsOrg[i].x,path.m_pathPointsOrg[i].y});return out;
}
static std::vector<int64_t> snapshot(CandidateStoredDrawing& path){
    std::vector<int64_t> out={path.m_width,path.m_ascent,path.m_descent,static_cast<int64_t>(path.m_pathOrg.Count())};
    for(size_t i=0;i<path.m_pathOrg.Count();++i){auto point=path.m_pathOrg.GetPoint<CPoint>(i);out.insert(out.end(),{path.m_pathOrg.Type(i),point.x,point.y});}return out;
}
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);}
static void transfer(OriginalStoredDrawing& a,CandidateStoredDrawing& b,int count,int failure){
    drawingAllocationFailure=0;drawingAllocationCalls=0;PathData x(count),y(count);
    for(int attempt=0;attempt<2;++attempt){
        drawingAllocationFailure=attempt==0?failure:0;drawingAllocationCalls=0;drawingAllocationEvents.clear();seed();bool ra=a.CreatePath(&x);int ea=errno;int fa=std::fetestexcept(FE_ALL_EXCEPT);auto events=drawingAllocationEvents;auto state=x.snapshot();
        drawingAllocationCalls=0;drawingAllocationEvents.clear();seed();bool rb=b.CreatePath(&y);int eb=errno;int fb=std::fetestexcept(FE_ALL_EXCEPT);
        if(ra!=rb || ea!=eb || fa!=fb || events!=drawingAllocationEvents || state!=y.snapshot()){fprintf(stderr,"Drawing handoff mismatch case=%llu count=%d failure=%d attempt=%d\n",(unsigned long long)comparisons,count,failure,attempt);std::abort();}
        ++handoffs;failedAllocations+=!ra && !events.empty();bytes+=state.size();
    }
}
static void compare(const std::vector<DrawingToken>& tokens,double xscale,double yscale,int baseline,double fontX,double fontY){
    OriginalStoredDrawing a(tokens);CandidateStoredDrawing b(tokens);
    a.m_scalex=b.m_scalex=xscale;a.m_scaley=b.m_scaley=yscale;a.m_baseline=b.m_baseline=baseline;a.m_style.style=b.m_style.style={fontX,fontY};
    seed();bool ra=a.ParseStr();int ea=errno;int fa=std::fetestexcept(FE_ALL_EXCEPT);seed();bool rb=b.ParseStr();int eb=errno;int fb=std::fetestexcept(FE_ALL_EXCEPT);
    if(ra!=rb || ea!=eb || fa!=fb || snapshot(a)!=snapshot(b) || a.requests!=b.requests){fprintf(stderr,"Stored drawing parse mismatch case=%llu result=%d/%d fp=%d/%d\n",(unsigned long long)comparisons,ra,rb,fa,fb);std::abort();}
    ++comparisons;fpFlags+=fa!=0;vertices+=a.m_pathTypesOrg.GetCount();
    OriginalStoredDrawing ac(a);CandidateStoredDrawing bc(b);++copies;
    if(snapshot(ac)!=snapshot(bc))std::abort();
    if(a.m_pathTypesOrg.GetCount()){
        a.m_pathPointsOrg[0].x+=1;auto point=b.m_pathOrg.GetPoint<CPoint>(0);b.m_pathOrg.SetPoint(0,point.x+1,point.y);
        if(snapshot(a)!=snapshot(b) || snapshot(ac)!=snapshot(bc))std::abort();
        auto previous=snapshot(ac);ac.m_str=bc.m_str=CStringW(std::vector<DrawingToken>());
        seed();bool ca=ac.ParseStr();ea=errno;fa=std::fetestexcept(FE_ALL_EXCEPT);seed();bool cb=bc.ParseStr();eb=errno;fb=std::fetestexcept(FE_ALL_EXCEPT);
        if(!ca || !cb || ea!=eb || fa!=fb || previous!=snapshot(ac) || snapshot(ac)!=snapshot(bc) || !ac.requests.empty() || !bc.requests.empty())std::abort();
    }
    for(int count:{0,1,3,8})for(int failure=0;failure<3;++failure)transfer(ac,bc,count,failure);
    if(!drawingAllocations.empty())std::abort();
}
static DrawingToken token(wchar_t command,const std::vector<CPoint>& points){DrawingToken result={command,{}};for(const auto& point:points)result.points.push_back({true,point});return result;}
int main(){
    const std::vector<std::vector<DrawingToken>> paths={
        {token('m',{{0,0}}),token('l',{{64,0},{64,64},{0,64}})},
        {token('m',{{-65,-129},{1,2}}),token('s',{{31,32},{63,64},{95,96}}),token('c',{}),token('c',{})},
        {token('m',{{0,0}}),token('b',{{1,2},{3,4}})},
        {token('m',{{0,0}}),token('s',{{1,2},{3,4}})},
        {token('n',{{1,2}}),token('m',{{3,4}})},
        {token('x',{{1,2}})},
        {token('m',{{1,2}}),token('m',{})},
    };
    for(const auto& path:paths)for(double xs:{-2.0,-0.5,0.0,0.1,1.0,1.5})for(double ys:{-1.5,0.0,0.3333333333333333,1.0,2.0})for(int baseline:{-9,0,11})for(double font:{0.0,50.0,100.0,123.25})compare(path,xs,ys,baseline,font,200-font);
    uint32_t random=0x3c2e9b17;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned iteration=0;iteration<4000;++iteration){std::vector<DrawingToken> path={token('m',{{0,0}})};for(unsigned i=0,count=next()%15;i<count;++i){std::vector<CPoint> points;for(unsigned j=0,n=next()%8;j<n;++j)points.push_back(CPoint(static_cast<int>(next()%200001)-100000,static_cast<int>(next()%200001)-100000));const wchar_t commands[]={L'm',L'n',L'l',L'b',L's',L'p',L'c',L'X'};path.push_back(token(commands[next()%8],points));}compare(path,0.125,1.75,static_cast<int>(next()%25)-12,123.25,77.5);}
    if(!vertices || !copies || !handoffs || !failedAllocations || !storedCopies || !storedSwaps || !storedPointUpdates || !storedClears || !drawingAllocations.empty())std::abort();
    printf("%llu exact persistent drawing histories passed (%llu retained vertices / %llu independent copies / %llu raster handoffs / %llu modeled allocation failures / %llu output byte-and-metadata observations, %llu Rust copies / %llu owner transfers / %llu coordinate updates / %llu clears; %llu FP-flag observations include seeded flags; native scaling, cache bypass, copy independence, guarded outputs, partial realloc failure and retry; synthetic token/point and allocation boundaries, finite representable geometry, native OS allocation failures excluded)\n",(unsigned long long)comparisons,(unsigned long long)vertices,(unsigned long long)copies,(unsigned long long)handoffs,(unsigned long long)failedAllocations,(unsigned long long)bytes,(unsigned long long)storedCopies,(unsigned long long)storedSwaps,(unsigned long long)storedPointUpdates,(unsigned long long)storedClears,(unsigned long long)fpFlags);
}
