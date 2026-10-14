// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_path_align.h"
static uint64_t comparisons=0,points=0,excluded=0,changed=0,clears=0;
static bool fits(int64_t value){return value>=INT32_MIN && value<=INT32_MAX;}
static bool defined(const std::vector<POINT>& input){
    if(input.empty())return true;
    int64_t minx=INT32_MAX,miny=INT32_MAX,maxx=INT32_MIN,maxy=INT32_MIN;
    for(const auto& p:input){if(p.x<minx)minx=p.x;if(p.y<miny)miny=p.y;if(p.x>maxx)maxx=p.x;if(p.y>maxy)maxy=p.y;}
    if(!fits(maxx+7) || !fits(maxy+7))return false;
    minx=(minx>>3)&~int64_t(7);miny=(miny>>3)&~int64_t(7);maxx=(maxx+7)>>3;maxy=(maxy+7)>>3;
    if(!fits(minx*8) || !fits(miny*8) || !fits(maxx+1) || !fits(maxy+1) || !fits(maxx+1-minx) || !fits(maxy+1-miny))return false;
    for(const auto& p:input)if(!fits(p.x-minx*8) || !fits(p.y-miny*8))return false;return true;
}
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static unsigned long lastError(){
#ifdef _WIN32
    return GetLastError();
#else
    return 0;
#endif
}
static void compare(const std::vector<POINT>& input,int emptyCount=0){
    if(!defined(input)){++excluded;return;}
    int count=input.empty()?emptyCount:static_cast<int>(input.size());
    ReferenceAlign::PathData a(input,count);CandidateAlign::PathData b(input,count);auto before=a.snapshot();
    CPoint oa(91,92),ob(91,92);CSize sa(93,94),sb(93,94);
    seed();a.AlignLeftTop(&oa,&sa);int ea=errno;unsigned long la=lastError();int fa=std::fetestexcept(FE_ALL_EXCEPT);
    seed();b.AlignLeftTop(&ob,&sb);int eb=errno;unsigned long lb=lastError();int fb=std::fetestexcept(FE_ALL_EXCEPT);
    if(ea!=eb || la!=lb || fa!=fb || oa.x!=ob.x || oa.y!=ob.y || sa.cx!=sb.cx || sa.cy!=sb.cy || a.snapshot()!=b.snapshot()){fprintf(stderr,"Path alignment mismatch case=%llu count=%d\n",(unsigned long long)comparisons,count);std::abort();}
    ++comparisons;points+=input.size();changed+=before!=a.snapshot();clears+=a.clears;
}
int main(){
    for(int x=-130;x<=130;++x)for(int y=-66;y<=66;++y)compare({{x,y},{x+63,y+7},{x-1,y-1}});
    const int32_t edges[]={INT32_MIN,INT32_MIN+1,INT32_MIN+63,-65,-64,-1,0,1,63,64,INT32_MAX-8,INT32_MAX-7,INT32_MAX};
    for(int32_t x:edges)for(int32_t y:edges)for(int32_t x2:edges)for(int32_t y2:edges)compare({{x,y},{x2,y2}});
    uint32_t random=0x9e32c1b7;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned iteration=0;iteration<24000;++iteration){std::vector<POINT> input;for(unsigned i=0,count=next()%65;i<count;++i)input.push_back({static_cast<int32_t>(next()%2000001)-1000000,static_cast<int32_t>(next()%2000001)-1000000});compare(input);}
    for(int count=-9;count<=0;++count)compare({},count);
    if(!comparisons || !points || !changed || !clears || !excluded || !alignHandled || alignFallbacks!=clears)std::abort();
    printf("%llu exact complete path-alignment comparisons passed (%llu point observations / %llu changed buffers / %llu native empty-path clears, %llu Rust alignments / %llu empty-path fallbacks; %llu arithmetic-domain exclusions, every nearby rounding boundary, negative coordinates and integer limits, guarded point/type buffers, exact origin/size and errno/FP/Windows error state; initialized buffers, disjoint geometry outputs, native cleanup modeled)\n",(unsigned long long)comparisons,(unsigned long long)points,(unsigned long long)changed,(unsigned long long)clears,(unsigned long long)alignHandled,(unsigned long long)alignFallbacks,(unsigned long long)excluded);
}
