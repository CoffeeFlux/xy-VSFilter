// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_span_coverage.h"
static uint64_t calls=0,fields=0,observations=0,failures=0,changed=0,retained=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
struct Effects {int error,flags;unsigned long last;Effects():error(errno),flags(std::fetestexcept(FE_ALL_EXCEPT)),last(0){
#ifdef _WIN32
last=GetLastError();
#endif
}bool operator==(const Effects& o)const{return error==o.error&&flags==o.flags&&last==o.last;}};
static uint64_t pack(int x,int y){return (static_cast<uint64_t>(static_cast<uint32_t>(y+0x40000000))<<32)|static_cast<uint32_t>(x+0x40000000);}
static tSpan span(int x1,int x2,int y){return {pack(x1,y),pack(x2,y+7)};}// End-row bits are intentionally irrelevant.
static void compare(const ScanLineData2& input,int xsub,int ysub,int fail=-1,bool nullOverlay=false){
    allocationFailure=-1;
    SharedPtrOverlay a=nullOverlay?SharedPtrOverlay():std::make_shared<Overlay>(),b=nullOverlay?SharedPtrOverlay():std::make_shared<Overlay>();
    if(a){a->mBody.reset(static_cast<BYTE*>(xy_malloc(7)),xy_free);a->mBorder.reset(static_cast<BYTE*>(xy_malloc(9)),xy_free);b->mBody.reset(static_cast<BYTE*>(xy_malloc(7)),xy_free);b->mBorder.reset(static_cast<BYTE*>(xy_malloc(9)),xy_free);}
    for(unsigned round=0;round<2;++round){
        coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;const size_t beforeA=g_xy_malloc_used_size;seed();bool ra=ReferenceCoverage::Rasterizer::Rasterize(input,xsub,ysub,a);Effects ea;const size_t deltaA=g_xy_malloc_used_size-beforeA;const auto ta=coverageEvents;const auto sa=a?a->Snapshot():std::vector<uint64_t>();
        coverageEvents.clear();allocationAttempt=0;allocationFailure=fail;const size_t beforeB=g_xy_malloc_used_size;seed();bool rb=CandidateCoverage::Rasterizer::Rasterize(input,xsub,ysub,b);Effects eb;const size_t deltaB=g_xy_malloc_used_size-beforeB;const auto tb=coverageEvents;const auto sb=b?b->Snapshot():std::vector<uint64_t>();
        if(deltaA!=deltaB||ra!=rb||!(ea==eb)||ta!=tb||sa!=sb){fprintf(stderr,"Coverage mismatch call=%llu xsub=%d ysub=%d failure=%d\n",(unsigned long long)calls,xsub,ysub,fail);std::abort();}
        ++calls;failures+=!ra;fields+=sa.size();observations+=ta.size();
        if(a&&a->mBody){for(const auto& slot:coverageAllocations)if(slot.data()==a->mBody.get()){changed+=std::any_of(slot.data(),slot.data()+slot.count,[](BYTE v){return v!=0;});break;}}
    }
    // Native copies of the shared plane must outlive the overlay that made it.
    std::shared_ptr<BYTE> aBody=a?a->mBody:std::shared_ptr<BYTE>(),aBorder=a?a->mBorder:std::shared_ptr<BYTE>();
    std::shared_ptr<BYTE> bBody=b?b->mBody:std::shared_ptr<BYTE>(),bBorder=b?b->mBorder:std::shared_ptr<BYTE>();
    const size_t held=g_xy_malloc_used_size;
    coverageEvents.clear();if(a)a->CleanUp();auto ta=coverageEvents;coverageEvents.clear();if(b)b->CleanUp();if(ta!=coverageEvents||g_xy_malloc_used_size!=held)std::abort();
    if(aBody){if(*aBody!=*bBody)std::abort();++retained;}
    if(aBorder){if(*aBorder!=*bBorder)std::abort();++retained;}
    coverageEvents.clear();aBody.reset();aBorder.reset();ta=coverageEvents;coverageEvents.clear();bBody.reset();bBorder.reset();if(ta!=coverageEvents||!coverageAllocations.empty()||g_xy_malloc_used_size)std::abort();
}
int main(){
    ScanLineData2 input;input.m_scan_line_data=std::make_shared<ScanLineData>();input.m_scan_line_data->mWidth=64;input.m_scan_line_data->mHeight=64;input.mWideBorder=3;input.mPathOffsetX=-7;input.mPathOffsetY=11;
    for(int x1=0;x1<=64;++x1)for(int x2=0;x2<=64;++x2)for(int phase:{0,1,7}){
        input.m_scan_line_data->mOutline={span(x1,x2,phase)};input.mWideOutline.clear();compare(input,phase,7-phase);
    }
    for(unsigned repetitions:{1u,2u,31u,32u,33u,65u})for(int y=0;y<64;++y){
        input.m_scan_line_data->mOutline.assign(repetitions,span(1,63,y));input.mWideOutline.assign(repetitions,span(-1,65,y));
        for(int fail:{-1,0,1})compare(input,y&7,(y+3)&7,fail);
    }
    input.mWideOutline.clear();
    for(const auto& odd:std::vector<tSpan>{span(-3,10,8),span(-7,-1,8),span(40,44,-8),span(128,136,-8),span(127,140,0)}){
        // For y=-8, x>=128 compensates the preceding row. Smaller x is tested
        // separately with a compact pitch in the Rust-only bounds check.
        int y=static_cast<int>((odd.first>>32)-0x40000000);int x=static_cast<int>((odd.first&0xffffffff)-0x40000000);
        if(y<0&&x<128)continue;
        input.m_scan_line_data->mOutline={odd};compare(input,0,0);
    }
    uint32_t random=0x13798642;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned test=0;test<12000;++test){
        input.m_scan_line_data->mWidth=8+next()%128;input.m_scan_line_data->mHeight=8+next()%96;input.mWideBorder=next()%24;input.m_scan_line_data->mOutline.clear();input.mWideOutline.clear();
        for(unsigned count=next()%40,i=0;i<count;++i){int x1=next()%input.m_scan_line_data->mWidth,x2=next()%input.m_scan_line_data->mWidth,y=next()%input.m_scan_line_data->mHeight;input.m_scan_line_data->mOutline.push_back(span(x1,x2,y));if(next()&1)input.mWideOutline.push_back(span(x1,x2,y));}
        compare(input,static_cast<int32_t>(next()),static_cast<int32_t>(next()));
    }
    input.m_scan_line_data->mOutline.clear();input.mWideOutline.clear();compare(input,0,0,-1,true);
    for(int zero:{0,1}){input.m_scan_line_data->mWidth=zero?0:64;input.m_scan_line_data->mHeight=zero?64:0;compare(input,0,0);}
    if(!calls||!changed||!failures||!coverageRustSpans||coverageFallbacks||!planeOwners||planeOwners!=planeLoans||planeOwners!=planeReleases||!retained)std::abort();
    printf("%llu exact whole-Rasterize calls passed (%llu plane bytes and metadata / %llu logical allocation-cleanup observations / %llu changed body calls / %llu false returns / %llu Rust span accumulations / %llu fallbacks / %llu Rust plane owners, loans and releases / %llu retained shared planes; endpoint phases, repeated byte wrapping, body/border order, ignored end-row bits, valid linear cross-row addressing, null/empty/reuse state, modeled first/second allocation failure and errno/FP/Windows error state; native dimension overflow, out-of-backing spans, unsupported signed shifts and actual allocator failure excluded; native guards and Rust ownership tests have distinct scopes)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)observations,(unsigned long long)changed,(unsigned long long)failures,(unsigned long long)coverageRustSpans,(unsigned long long)coverageFallbacks,(unsigned long long)planeOwners,(unsigned long long)retained);
}
