// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_span_merge.h"
static uint64_t comparisons=0,spans=0,changed=0,excluded=0,aliasExcluded=0,repeated=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);}
static bool compare(const tSpanBuffer& a,const tSpanBuffer& b,int dx,int dy,bool alias=false,tSpanBuffer* result=NULL){
    tSpanBuffer ar=a,ac=a,br=b,bc=b;ar.reserve(a.size()+7);ac.reserve(a.size()+7);br.reserve(b.size()+3);bc.reserve(b.size()+3);
    seed();bool unresolved=false;try{ReferenceSpans::OverlapRegion(ar,alias?ar:br,dx,dy);}catch(const UnresolvedSpanTail&){unresolved=true;}
    int ea=errno;int fa=std::fetestexcept(FE_ALL_EXCEPT);
    if(unresolved){
        // Do not invoke the candidate's native fallback for excluded control flow.
        // The direct Rust planner must decline, not label that input a success.
        uint64_t first=(static_cast<int64_t>(dy)<<32)-dx,second=(static_cast<int64_t>(dy)<<32)+dx;tSpanBuffer empty;
        XyRust::SpanMerge probe(a,alias?empty:b,first,second);
        if(probe.Ready()){fprintf(stderr,"Unresolved span tail incorrectly accepted\n");std::abort();}
        ++excluded;aliasExcluded+=alias;return false;
    }
    seed();CandidateSpans::OverlapRegion(ac,alias?ac:bc,dx,dy);int eb=errno;int fb=std::fetestexcept(FE_ALL_EXCEPT);
    if(ar!=ac || ar.capacity()!=ac.capacity() || br!=bc || br.capacity()!=bc.capacity() || ea!=eb || fa!=fb){fprintf(stderr,"Span mismatch case=%llu a=%zu b=%zu dx=%d dy=%d alias=%d results=%zu/%zu\n",(unsigned long long)comparisons,a.size(),b.size(),dx,dy,alias,ar.size(),ac.size());std::abort();}
    ++comparisons;spans+=ar.size();changed+=ar!=a;if(result)*result=ar;return true;
}
static std::vector<tSpanBuffer> inputs(){
    const tSpan choices[]={{0,0},{0,1},{1,3},{3,4},{9,2},{7,9},{UINT64_MAX-1,UINT64_MAX},{UINT64_MAX,0}};
    std::vector<tSpanBuffer> out(1);for(const auto& a:choices){out.push_back({a});for(const auto& b:choices)out.push_back({a,b});}return out;
}
int main(){
    auto values=inputs();for(const auto& a:values)for(const auto& b:values)for(int dx:{INT32_MIN,-3,0,2,INT32_MAX})for(int dy:{0,1,INT32_MAX})compare(a,b,dx,dy);
    for(const auto& input:values)compare(input,{},3,0,true);
    uint32_t random=0x24f8c13b;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned iteration=0;iteration<16000;++iteration){
        tSpanBuffer input;uint64_t position=0x4000000040000000ull;unsigned count=next()%33;
        for(unsigned i=0;i<count;++i){position+=next()%17;uint64_t end=position+next()%33;input.push_back({position,end});position=end+1;}
        int dx=static_cast<int>(next()%33);tSpanBuffer first,second;
        if(!compare({},input,dx,0,false,&first) || !compare(first,input,dx,0,false,&second))std::abort();++repeated;
    }
    if(!comparisons || !spans || !changed || !excluded || !aliasExcluded || !repeated || !spanHandled || spanFallbacks)std::abort();
    printf("%llu exact terminating outline-span comparisons passed (%llu emitted spans / %llu changed outputs / %llu repeated widening histories / %llu Rust merges / %llu fallbacks; %llu leftover-A control-flow exclusions including %llu nonempty aliases, excluded branches guarded before their native loop and direct Rust decline checked; exact order, touching/tied/reversed spans, unsigned wrap, vector capacity and errno/FP flags; native offset calculation retained, negative dy and allocation failure excluded)\n",(unsigned long long)comparisons,(unsigned long long)spans,(unsigned long long)changed,(unsigned long long)repeated,(unsigned long long)spanHandled,(unsigned long long)spanFallbacks,(unsigned long long)excluded,(unsigned long long)aliasExcluded);
}
