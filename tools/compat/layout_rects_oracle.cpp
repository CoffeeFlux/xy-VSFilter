// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_layout_rects.h"
static void layoutSeed(){
    errno=EDOM;
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static uint32_t layoutLastError(){
#ifdef _WIN32
    return GetLastError();
#else
    return 0;
#endif
}
static uint64_t sequences=0,operations=0,allocations=0,advances=0,clears=0,observations=0,moved=0,reused=0;
static std::vector<int32_t> rect(const CRect& r){return {r.left,r.top,r.right,r.bottom};}
static std::vector<int32_t> snapshot(ReferenceLayout::CScreenLayoutAllocator& a){
    std::vector<int32_t> out;POSITION pos=a.m_subrects.GetHeadPosition();
    while(pos){const auto& r=a.m_subrects.GetNext(pos);out.insert(out.end(),{r.r.left,r.r.top,r.r.right,r.r.bottom,r.segment,r.entry,r.layer});}return out;
}
static std::vector<int32_t> snapshot(CandidateLayout::CScreenLayoutAllocator& a){
    std::vector<int32_t> out;
#ifdef LAYOUT_DEFAULT_CHECK
    POSITION pos=a.m_subrects.GetHeadPosition();while(pos){const auto& r=a.m_subrects.GetNext(pos);out.insert(out.end(),{r.r.left,r.r.top,r.r.right,r.r.bottom,r.segment,r.entry,r.layer});}
#else
    for(size_t i=0;i<a.m_subrects.GetCount();++i){auto r=a.m_subrects.Get<CandidateLayout::CScreenLayoutAllocator::SubRect>(i);out.insert(out.end(),{r.r.left,r.r.top,r.r.right,r.r.bottom,r.segment,r.entry,r.layer});}
#endif
    return out;
}
struct Pair {
    ReferenceLayout::CScreenLayoutAllocator a;CandidateLayout::CScreenLayoutAllocator b;
    Pair(){++sequences;}
    void check(int ea,uint32_t la){
        int eb=errno;uint32_t lb=layoutLastError();
        auto x=snapshot(a),y=snapshot(b);++operations;observations+=x.size();
        if(x!=y || ea!=eb || la!=lb){fprintf(stderr,"Layout state mismatch sequence=%llu operation=%llu records=%zu/%zu errno=%d/%d\n",(unsigned long long)sequences,(unsigned long long)operations,x.size()/7,y.size()/7,ea,eb);std::abort();}
    }
    void alloc(CRect bounds,int top,int bottom,int alignment,int segment,int entry,int layer,int collisions){
        CSubtitle s={bounds,top,bottom,alignment};size_t count=a.m_subrects.GetCount();layoutSeed();
        CRect ra=a.AllocRect(&s,segment,entry,layer,collisions);int ea=errno;uint32_t la=layoutLastError();layoutSeed();
        CRect rb=b.AllocRect(&s,segment,entry,layer,collisions);check(ea,la);
        if(rect(ra)!=rect(rb) || rect(s.m_rect)!=rect(bounds) || s.m_topborder!=top || s.m_bottomborder!=bottom || s.m_scrAlignment!=alignment){fprintf(stderr,"Layout return/input mismatch sequence=%llu\n",(unsigned long long)sequences);std::abort();}
        moved+=rect(ra)!=rect(bounds);reused+=a.m_subrects.GetCount()==count;++allocations;
    }
    void advance(int segment,const std::vector<int>& active){CAtlArray<int> entries;for(int v:active)entries.Add(v);layoutSeed();a.AdvanceToSegment(segment,entries);int ea=errno;uint32_t la=layoutLastError();layoutSeed();b.AdvanceToSegment(segment,entries);++advances;check(ea,la);}
    void clear(){layoutSeed();a.Empty();int ea=errno;uint32_t la=layoutLastError();layoutSeed();b.Empty();++clears;check(ea,la);}
};
static uint32_t randomState=0x61d94a27;
static uint32_t randomWord(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
int main(){
    for(int width=-2;width<=5;++width)for(int height=-2;height<=5;++height)for(int top=-2;top<=2;++top)for(int bottom=-2;bottom<=2;++bottom)for(int alignment=-1;alignment<=10;++alignment){
        Pair p;CRect base(10,10,10+width,10+height);
        p.alloc(base,top,bottom,alignment,2,1,0,0);
        p.alloc(base,top,bottom,alignment,2,2,0,1);
        p.alloc(base,top,bottom,alignment,2,3,1,-1);
        p.alloc(CRect(99,99,101,101),bottom,top,9-alignment,2,1,-7,1);
        p.advance(3,{1,3,1});p.advance(2,{3});p.alloc(base,top,bottom,alignment,2,1,0,0);p.advance(0,{1,3});p.clear();
    }
    for(unsigned sequence=0;sequence<24000;++sequence){
        Pair p;int segment=0;
        for(unsigned step=0;step<24;++step){
            unsigned action=randomWord()%8;
            if(action==0){p.clear();}
            else if(action<3){segment+=int(randomWord()%5)-2;std::vector<int> active;for(int i=0;i<8;++i)if(randomWord()&1)active.push_back(i);p.advance(segment,active);}
            else {int x=int(randomWord()%101)-50,y=int(randomWord()%101)-50;int w=int(randomWord()%31)-3,h=int(randomWord()%31)-3;p.alloc(CRect(x,y,x+w,y+h),int(randomWord()%9)-4,int(randomWord()%9)-4,int(randomWord()%13)-2,segment,int(randomWord()%8),int(randomWord()%5)-2,int(randomWord()%3)-1);}
        }
    }
    // Edge identifiers are tested in separate short histories; subtraction,
    // abs and all rectangle calculations stay in their representable domains.
    for(int segment:{INT32_MIN+2,-2,0,2,INT32_MAX-2})for(int entry:{INT32_MIN,-1,0,1,INT32_MAX})for(int layer:{INT32_MIN,0,INT32_MAX}){
        Pair p;p.alloc(CRect(0,0,8,8),1,2,2,segment,entry,layer,0);p.advance(segment-1,{entry,entry});p.alloc(CRect(0,0,9,9),2,3,8,segment-1,entry,0,1);p.advance(segment+1,{entry});p.clear();
    }
    if(!moved || !reused || !layoutAdds || !layoutAdvanceHandled || !layoutPlaceHandled || layoutAdvanceFallback || layoutPlaceFallback || !layoutFinds || !layoutClears){fprintf(stderr,"Layout route controls failed\n");return 1;}
    printf("%llu Rust segment-retention selections and %llu Rust collision placements passed (%llu retention fallbacks / %llu placement fallbacks; checked arithmetic declines retain native fallback before any state/output mutation)\n",(unsigned long long)layoutAdvanceHandled,(unsigned long long)layoutPlaceHandled,(unsigned long long)layoutAdvanceFallback,(unsigned long long)layoutPlaceFallback);
    printf("%llu exact layout sequences / %llu operations passed (%llu allocations / %llu advances / %llu clears, %llu moved returns / %llu reused records, %llu retained record field observations, %llu Rust owners / %llu adds / %llu individual removal calls / %llu individual segment writes / %llu lookups / %llu clears; first matching entry ignores layer, forward/backward adjacent segments, ordered collisions, borders, empty/inverted rectangles, all alignments and ignored collision modes; native border geometry retained, errno/Windows LastError, representable arithmetic and allocation-success scope)\n",(unsigned long long)sequences,(unsigned long long)operations,(unsigned long long)allocations,(unsigned long long)advances,(unsigned long long)clears,(unsigned long long)moved,(unsigned long long)reused,(unsigned long long)observations,(unsigned long long)layoutOwners,(unsigned long long)layoutAdds,(unsigned long long)layoutRemoves,(unsigned long long)layoutUpdates,(unsigned long long)layoutFinds,(unsigned long long)layoutClears);
}
