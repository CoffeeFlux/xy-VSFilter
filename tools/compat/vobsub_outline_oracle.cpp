// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "original_vobsub_outline.h"
#include <array>
#include <memory>
struct Outcome {
    std::vector<int32_t> points;
    std::vector<uint64_t> events;
    int32_t x,y;bool present;int allocations,refusals,assertions,observations;
    bool operator==(const Outcome& other)const{return points==other.points && events==other.events && x==other.x && y==other.y && present==other.present && allocations==other.allocations && refusals==other.refusals && assertions==other.assertions && observations==other.observations;}
};
static uint64_t comparisons=0,observations=0,refusals=0,assertions=0,nonempty=0;
static uint64_t directSteps=0,directAssertions=0;
template<class Image> static Outcome execute(const std::vector<RGBQUAD>& input,int width,int height,int failure) {
    auto pixels=input;Image image;image.rect=CRect(-7,13,width-7,height+13);image.lpPixels=pixels.data();
    CPoint topLeft(-123,456);outlineTopLeft=&topLeft;outlineMask=nullptr;outlineMaskCount=0;
    outlineEvents.clear();outlineFailure=failure;outlineAllocations=outlineRefusals=outlineObservations=outlineAssertions=0;
    std::unique_ptr<CAutoPtrList<COutline>> outlines(image.GetOutlineList(topLeft));
    Outcome result;result.present=!!outlines;result.x=topLeft.x;result.y=topLeft.y;
    if(outlines)for(const auto* o:outlines->values) {
        result.points.push_back(static_cast<int32_t>(o->pa.values.size()));
        for(size_t i=0;i<o->pa.values.size();++i)result.points.insert(result.points.end(),{o->pa.values[i].x,o->pa.values[i].y,o->da.values[i]});
    }
    result.events=outlineEvents;result.allocations=outlineAllocations;result.refusals=outlineRefusals;result.assertions=outlineAssertions;result.observations=outlineObservations;
    if(pixels.size()!=input.size() || (!pixels.empty() && std::memcmp(pixels.data(),input.data(),pixels.size()*sizeof(RGBQUAD))))std::abort();
    return result;
}
static void compare(const std::vector<RGBQUAD>& pixels,int width,int height,int failure=-1) {
    const auto a=execute<ReferenceVobOutline>(pixels,width,height,failure);
    const auto b=execute<CandidateVobOutline>(pixels,width,height,failure);
    if(!(a==b)){fprintf(stderr,"VobSub outline mismatch width=%d height=%d failure=%d\n",width,height,failure);std::abort();}
    ++comparisons;observations+=a.observations;refusals+=a.refusals;assertions+=a.assertions;nonempty+=!a.points.empty();
}
static std::vector<RGBQUAD> image(int innerWidth,int innerHeight,uint32_t mask,unsigned seed) {
    const int width=innerWidth+2,height=innerHeight+2;
    std::vector<RGBQUAD> result(static_cast<size_t>(width*height),RGBQUAD{1,2,3,0});
    for(int y=0;y<innerHeight;++y)for(int x=0;x<innerWidth;++x) {
        auto& p=result[(y+1)*width+x+1];p.rgbBlue=static_cast<BYTE>(x*19+seed);p.rgbGreen=static_cast<BYTE>(y*31+seed);p.rgbRed=static_cast<BYTE>(x*7+y*11+seed);
        p.rgbReserved=(mask>>((y*innerWidth+x)%32))&1 ? static_cast<BYTE>(1+(seed+x+y)%255) : 0;
    }
    return result;
}
static void compareStep(const std::vector<BYTE>& bytes,int x,int y,int direction) {
    CPoint topLeft(-123,456);outlineTopLeft=&topLeft;
    auto a=bytes,b=bytes;outlineMask=a.data();outlineMaskCount=a.size();outlineEvents.clear();outlineObservations=outlineAssertions=0;
    const auto stateA=ReferenceVobOutlineWalk(a,3,3,x,y,direction);const auto eventsA=outlineEvents;const int assertionsA=outlineAssertions;
    outlineMask=b.data();outlineEvents.clear();outlineObservations=outlineAssertions=0;
    const auto stateB=CandidateVobOutlineWalk(b,3,3,x,y,direction);
    if(a!=b || stateA!=stateB || eventsA!=outlineEvents || assertionsA!=outlineAssertions){fprintf(stderr,"VobSub direct walk mismatch x=%d y=%d dir=%d\n",x,y,direction);std::abort();}
    ++directSteps;directAssertions+=assertionsA;outlineMask=nullptr;outlineMaskCount=0;
}
int main() {
    for(unsigned mask=0;mask<65536;++mask)compare(image(4,4,mask,mask),6,6);
    uint32_t random=0x91234567;
    for(int width:{0,1,2,3,5,8})for(int height:{0,1,2,3,5,8})for(unsigned seed=0;seed<128;++seed) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        compare(image(width,height,random,seed),width+2,height+2);
    }
    for(int width:{0,1,2,7})for(int height:{0,1,2,7})for(BYTE alpha:{BYTE(0),BYTE(1),BYTE(255)}) {
        std::vector<RGBQUAD> pixels(static_cast<size_t>(width*height),RGBQUAD{1,2,3,alpha});compare(pixels,width,height);
    }
    for(unsigned mask:{0U,1U,0x8000U,0x8421U,0xa5a5U,0xffffU})for(int failure=0;failure<8;++failure)compare(image(4,4,mask,17),6,6,failure);
    const BYTE values[]={0,1,2,3,6,14,16,30,255};
    for(CPoint point:{CPoint(0,0),CPoint(2,0),CPoint(1,1),CPoint(0,2),CPoint(2,2)})for(int direction=0;direction<4;++direction)
    for(unsigned code=0;code<6561;++code) {
        std::vector<BYTE> grid(9,0);unsigned digits=code;
        grid[point.y*3+point.x]=values[digits%9];digits/=9;
        const CPoint neighbors[4][3]={
            {CPoint(point.x,point.y-1),CPoint(point.x+1,point.y-1),CPoint(point.x+1,point.y)},
            {CPoint(point.x+1,point.y),CPoint(point.x+1,point.y+1),CPoint(point.x,point.y+1)},
            {CPoint(point.x,point.y+1),CPoint(point.x-1,point.y+1),CPoint(point.x-1,point.y)},
            {CPoint(point.x-1,point.y),CPoint(point.x-1,point.y-1),CPoint(point.x,point.y-1)}};
        for(const auto& p:neighbors[direction]){const BYTE value=values[digits%9];digits/=9;if(p.x>=0 && p.x<3 && p.y>=0 && p.y<3)grid[p.y*3+p.x]=value;}
        compareStep(grid,point.x,point.y,direction);
    }
    const auto empty=execute<ReferenceVobOutline>(image(1,1,0,0),3,3,-1);
    const auto visible=execute<ReferenceVobOutline>(image(1,1,1,0),3,3,-1);
    if(!empty.present || !empty.points.empty() || empty.x!=INT_MAX || empty.y!=INT_MAX || visible.points.empty() || visible.x!=1 || visible.y!=1 || !maskHandled || !findHandled || !walkHandled || !refusals || !outlineFallback || !directAssertions){fprintf(stderr,"VobSub outline route/point controls failed\n");return 1;}
    printf("%llu exact VobSub outline comparisons and %llu direct walk steps passed (%llu native allocation/append/mask observations, %llu nonempty outline lists, %llu injected allocation refusals, %llu matching allocation assertions, %llu direct walk assertions, %llu Rust mask fills, %llu Rust start searches, %llu Rust walk steps, %llu native fallback calls; every bordered 4x4 occupancy mask, exact ordered points/turns, mark-byte wrapping and partial top-left/list state)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(directSteps),static_cast<unsigned long long>(observations),static_cast<unsigned long long>(nonempty),static_cast<unsigned long long>(refusals),static_cast<unsigned long long>(assertions),static_cast<unsigned long long>(directAssertions),static_cast<unsigned long long>(maskHandled),static_cast<unsigned long long>(findHandled),static_cast<unsigned long long>(walkHandled),static_cast<unsigned long long>(outlineFallback));
}
