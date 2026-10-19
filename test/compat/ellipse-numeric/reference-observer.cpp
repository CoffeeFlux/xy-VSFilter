// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup/candidate widened-region calls and every packed output span.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
template<class Tag,typename Tag::type Member>struct WideAccess {friend typename Tag::type member(Tag){return Member;}};
#define WFIELD(tag,type_,name) struct tag {typedef type_ ScanLineData2::*type;friend type member(tag);};template struct WideAccess<tag,&ScanLineData2::name>;
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
WFIELD(WideOutline,tSpanBuffer,mWideOutline)
WFIELD(WideBorder,int,mWideBorder)
#endif
static void seed_path(PathData& p,const std::vector<POINT>& points,const std::vector<BYTE>& types){
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points=points;snapshot.types=types;p.Replace(snapshot);
#else
    p.mPathPoints=static_cast<int>(points.size());p.mpPathPoints=static_cast<POINT*>(malloc(points.size()*sizeof(POINT)));p.mpPathTypes=static_cast<BYTE*>(malloc(types.size()));
    if(!points.empty()){check(p.mpPathPoints&&p.mpPathTypes);memcpy(p.mpPathPoints,points.data(),points.size()*sizeof(POINT));memcpy(p.mpPathTypes,types.data(),types.size());}
#endif
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    auto scan=std::make_shared<ScanLineData>();ScanLineData2 wide(CPoint(7,-9),scan);unsigned index=0;
    printf("widen pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const int radii[][2]={{-3,-5},{0,0},{1,0},{8,0},{0,1},{1,1},{3,7},{7,3},{15,19},{19,15},{64,64},{65,65},{128,33},{33,128}};
    for(unsigned scenario=0;scenario<24;++scenario){
        std::vector<POINT> points;std::vector<BYTE> types;uint32_t rng=0x4b19de37u+scenario;
        for(unsigned i=0;i<8;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            points.push_back(POINT{static_cast<long>(rng%2048)-1024,static_cast<long>((rng>>12)%1024)});types.push_back(i?2:6);}
        if(scenario%8==1){types[3]=6;types[6]=6;}
        if(scenario%8==2){types[3]=0xfe;types[5]=0xff;}
        if(scenario%8==3){types[1]=types[2]=types[3]=4;types[4]=types[5]=types[6]=4;}
        if(scenario%8==4){types[1]=types[2]=types[3]=0xfc;types[4]=types[5]=types[6]=0xfc;}
        if(scenario%8==5){types[1]=types[2]=types[3]=0xfc;types[4]=0xfa;}
        if(scenario%8==6)for(auto& t:types)t|=1;
        if(scenario%8==7)for(auto& p:points)p.y=16;
        if(scenario==23){points.clear();types.clear();}
        PathData path;seed_path(path,points,types);scan->ScanConvert(path,CSize(256,128));
        for(unsigned radius=0;radius<14;++radius)for(unsigned sticky=0;sticky<2;++sticky){
            reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const Status before=status();
            const bool result=wide.CreateWidenedRegion(radii[radius][0],radii[radius][1]);const Status after=status();
            #ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            const auto snapshot=wide.Snapshot();const auto& spans=snapshot.mWideOutline;const int border=snapshot.mWideBorder;
#else
            const auto& spans=wide.*member(WideOutline{});const int border=wide.*member(WideBorder{});
#endif
            printf("widen-case index=%u scenario=%u radius=%u sticky=%u result=%u border=%d spans=%u",index++,scenario,radius,sticky,unsigned(result),border,unsigned(spans.size()));emit("before",before);emit("after",after);printf("\n");
            for(const auto& span:spans)printf("widen-span %016llx,%016llx\n",span.first,span.second);
        }
    }
    printf("widen-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
