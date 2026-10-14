// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_edge_storage.h"
static uint64_t histories=0,calls=0,spans=0,recordFields=0,observations=0,emptyCalls=0;
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
static void compare(const std::vector<BYTE>& types,int height,unsigned budget,uint32_t random){
    PathData path(types),empty({});ReferenceEdges::ScanLineData a(budget,random);CandidateEdges::ScanLineData b(budget,random);
    for(unsigned round=0;round<3;++round){
        const PathData& input=round==2?empty:path;a.trace.clear();b.trace.clear();a.records.clear();b.records.clear();CSize size(37,height);
        seed();bool ra=a.ScanConvert(input,size);Effects ea;seed();bool rb=b.ScanConvert(input,size);Effects eb;
        if(ra!=rb||!(ea==eb)||a.trace!=b.trace||a.records!=b.records||a.Snapshot()!=b.Snapshot()||b.m_edges.Ready()){fprintf(stderr,"Edge storage mismatch history=%llu round=%u height=%d budget=%u types=%zu\n",(unsigned long long)histories,round,height,budget,types.size());std::abort();}
        ++calls;emptyCalls+=input.mPathPoints==0;spans+=a.mOutline.size();recordFields+=a.records.size();observations+=a.trace.size();
    }++histories;
}
int main(){
    const std::vector<std::vector<BYTE>> paths={{},{6},{6,2,2,2,3},{6,4,4,4},{6,252,252,252},{6,2,2,250,250},{6,254,2,7,2},{0,2,2,2},{6,4,4},{6,2,6,2,6,2}};
    for(const auto& path:paths)for(int height:{0,1,3,17})for(unsigned budget:{0u,1u,3u,33u,1025u,4097u})compare(path,height,budget,0x1c3e8279);
    for(unsigned type=0;type<256;++type)compare({6,2,2,2,static_cast<BYTE>(type)},1+type%17,type%34,0x3e582719+type);
    uint32_t random=0x79e134b5;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<2000;++i){std::vector<BYTE> types={6};for(unsigned n=next()%33,j=0;j<n;++j){const BYTE choices[]={2,3,6,7,254,0,8,10};types.push_back(choices[next()%8]);}compare(types,next()%33,next()%19,next());}
    if(!histories||!spans||!emptyCalls||!edgeOwners||edgeOwners!=edgeResets||!edgeWrites||!edgeGrowths||edgeFallbacks)std::abort();
    printf("%llu exact scan-conversion histories / %llu calls passed (%llu emitted spans / %llu initialized edge-and-row fields / %llu geometry callback observations / %llu empty calls; %llu Rust stores / %llu edge writes / %llu growths / %llu scalar reads / %llu releases / %llu fallbacks; full pinned ScanConvert, native sorting/winding and outline capacity, row order, signed edge flags, all path-type bytes at bounded indices, reuse/empty state, errno/FP/Windows error state; line/curve emission is controlled, unused edge zero/tails, invalid links/cycles, native constructor state and OOM excluded)\n",(unsigned long long)histories,(unsigned long long)calls,(unsigned long long)spans,(unsigned long long)recordFields,(unsigned long long)observations,(unsigned long long)emptyCalls,(unsigned long long)edgeOwners,(unsigned long long)edgeWrites,(unsigned long long)edgeGrowths,(unsigned long long)edgeReads,(unsigned long long)edgeResets,(unsigned long long)edgeFallbacks);
}
