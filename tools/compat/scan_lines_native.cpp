// SPDX-License-Identifier: GPL-2.0-or-later
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
template<class Tag,typename Tag::type Member>struct Access {friend typename Tag::type member(Tag){return Member;}};
#define FIELD(tag,type_,name) struct tag {typedef type_ ScanLineData::*type;friend type member(tag);};template struct Access<tag,&ScanLineData::name>;
FIELD(Outline,tSpanBuffer,mOutline)
FIELD(Width,int,mWidth) FIELD(Height,int,mHeight)
FIELD(Next,unsigned,mEdgeNext) FIELD(Capacity,unsigned,mEdgeHeapSize)
FIELD(FirstSet,bool,fFirstSet) FIELD(First,CPoint,firstp) FIELD(Last,CPoint,lastp)
static void seed(PathData& p,const std::vector<POINT>& points,const std::vector<BYTE>& types){
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points=points;snapshot.types=types;p.Replace(snapshot);
#else
    p.mPathPoints=static_cast<int>(points.size());p.mpPathPoints=static_cast<POINT*>(malloc(points.size()*sizeof(POINT)));p.mpPathTypes=static_cast<BYTE*>(malloc(types.size()));
    if(!points.empty()){check(p.mpPathPoints&&p.mpPathTypes);memcpy(p.mpPathPoints,points.data(),points.size()*sizeof(POINT));memcpy(p.mpPathTypes,types.data(),types.size());}
#endif
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    ScanLineData scan;unsigned index=0;
    printf("scan-lines pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    for(unsigned scenario=0;scenario<192;++scenario){
        std::vector<POINT> points;std::vector<BYTE> types;uint32_t rng=0x4b19de37u+scenario;
        const unsigned count=scenario>=160?65:8;
        for(unsigned i=0;i<count;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            long x=static_cast<long>(rng%2048)-1024,y=static_cast<long>((rng>>12)%1024);
            if(scenario>=96&&scenario<128){const uint32_t raw[]={0x80000000u,0x7fffffffu,0xfffffff0u,0,0x40000000u,0xc0000000u,0x1000001u,0xff000001u};memcpy(&x,&raw[i%8],4);}
            points.push_back(POINT{x,y});types.push_back(i?2:6);
        }
        if(scenario%8==1){types[3]=6;types[6]=6;}
        if(scenario%8==2){types[3]=0xfe;types[5]=0xff;}
        if(scenario%8==3){types[1]=types[2]=types[3]=4;types[4]=types[5]=types[6]=4;}
        if(scenario%8==4){types[1]=types[2]=types[3]=0xfc;types[4]=types[5]=types[6]=0xfc;}
        if(scenario%8==5){types[1]=types[2]=types[3]=0xfc;types[4]=0xfa;}
        if(scenario%8==6)for(auto& t:types)t|=1;
        if(scenario%8==7){for(auto& p:points)p.y=16;}
        if(scenario&&scenario%23==0){points.clear();types.clear();}
        for(unsigned sticky=0;sticky<2;++sticky){PathData path;seed(path,points,types);reset();
            // Set a preexisting x87/SSE exception through the CRT, without any
            // geometry operation between reset and the actual method call.
            if(sticky)std::feraiseexcept(FE_DIVBYZERO);
            const Status before=status();const bool result=scan.ScanConvert(path,CSize(256+scenario,128));const Status after=status();
            const auto& spans=scan.*member(Outline{});const auto first=scan.*member(First{}),last=scan.*member(Last{});
            printf("scan-case index=%u scenario=%u sticky=%u points=%u result=%u size=%d,%d next=%u capacity=%u first=%u:%ld,%ld last=%ld,%ld spans=%u",index++,scenario,sticky,unsigned(points.size()),unsigned(result),scan.*member(Width{}),scan.*member(Height{}),scan.*member(Next{}),scan.*member(Capacity{}),unsigned(scan.*member(FirstSet{})),first.x,first.y,last.x,last.y,unsigned(spans.size()));emit("before",before);emit("after",after);printf("\n");
            for(const auto& span:spans)printf("scan-span %016llx,%016llx\n",span.first,span.second);
        }
    }
    printf("scan-lines-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
