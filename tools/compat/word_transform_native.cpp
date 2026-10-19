// SPDX-License-Identifier: GPL-2.0-or-later
// Actual private CWord method, reached by explicit template instantiation
// (access checking is not applied to explicit-instantiation arguments).
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <cache_manager.h>
#include <cfenv>
#include <process.h>
struct TransformTag {typedef void(CWord::*type)(PathData*,const CPointCoor2&);friend type method(TransformTag);};
template<class Tag,typename Tag::type Member>struct Access {friend typename Tag::type method(Tag){return Member;}};
template struct Access<TransformTag,&CWord::Transform>;
class Word:public CWord{public:explicit Word(const FwSTSStyle& s):CWord(s,L"transform",0,0,0){}bool CreatePath(PathData*)override{return false;}SharedPtrCWord Copy()override{return SharedPtrCWord();}};
static const POINT points[]={{13,-7},{-32,64},{0,0},{INT_MAX,INT_MIN},{16777217,-16777219}};
static void seed(PathData& p,bool empty){
    if(empty)return;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points.assign(points,points+5);snapshot.types.assign(5,255);p.Replace(snapshot);
#else
    p.mPathPoints=5;p.mpPathPoints=static_cast<POINT*>(malloc(sizeof(points)));p.mpPathTypes=static_cast<BYTE*>(malloc(5));check(p.mpPathPoints&&p.mpPathTypes);memcpy(p.mpPathPoints,points,sizeof(points));memset(p.mpPathTypes,255,5);
#endif
}
static void emit_path(const PathData& p){
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    const auto snapshot=p.Snapshot();const int n=static_cast<int>(snapshot.points.size());const POINT* xy=snapshot.points.data();const BYTE* types=snapshot.types.data();
#else
    const int n=p.mPathPoints;const POINT* xy=p.mpPathPoints;const BYTE* types=p.mpPathTypes;
#endif
    printf("transform-path count=%d hash=%08lx",n,PathDataTraits::Hash(p));for(int i=0;i<n;++i)printf(" %02x:%ld,%ld",unsigned(types[i]),xy[i].x,xy[i].y);printf("\n");
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    const uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,0x4059000000000000ull,0xc059000000000000ull,1,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000000000001ull,0x7ff0000000000001ull,0x7fefffffffffffffull,0x4046800000000000ull,0x3ff0000000000001ull};
    double STSStyle::* fields[]={&STSStyle::fontScaleX,&STSStyle::fontScaleY,&STSStyle::fontAngleZ,&STSStyle::fontAngleX,&STSStyle::fontAngleY,&STSStyle::fontShiftX,&STSStyle::fontShiftY};
    const CPoint origins[]={CPoint(0,0),CPoint(31,-65),CPoint(INT_MIN,INT_MAX)};
    std::vector<std::shared_ptr<FwSTSStyle>> owners;unsigned index=0;
    printf("transform pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    TransformTag::type volatile transform=method(TransformTag{});
    const double finite[]={0.125,-0.25,0.5,-1,1,1.125,12.75,45,90,120.25,179.999,270,360,1000};
    for(unsigned scenario=0;scenario<190;++scenario){
        const unsigned field=scenario<126?scenario/14:9+scenario-126,value=scenario%14;
        uint64_t mixed[9],rng=0x9e3779b97f4a7c15ull+scenario;
        for(unsigned i=0;i<9;++i){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;
            if(scenario<158)memcpy(&mixed[i],&finite[rng%14],8);else mixed[i]=values[rng%14];}
        STSStyle s;s.scrAlignment=1000+index;
        if(field<7)memcpy(&(s.*fields[field]),&values[value],8);
        if(scenario>=126)for(unsigned i=0;i<7;++i)memcpy(&(s.*fields[i]),&mixed[i],8);
        auto owner=std::make_shared<FwSTSStyle>(s);owners.push_back(owner);Word word(*owner);
        if(field==7)memcpy(&word.m_target_scale_x,&values[value],8);if(field==8)memcpy(&word.m_target_scale_y,&values[value],8);
        if(scenario>=126){memcpy(&word.m_target_scale_x,&mixed[7],8);memcpy(&word.m_target_scale_y,&mixed[8],8);}
        for(unsigned sticky=0;sticky<2;++sticky)for(unsigned origin=0;origin<3;++origin)for(unsigned round=0;round<2;++round)for(unsigned empty=0;empty<2;++empty){
            word.m_round_to_whole_pixel_after_scale_to_target=round!=0;PathData path;seed(path,empty!=0);reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);const Status before=status();
            (word.*transform)(&path,origins[origin]);const Status after=status();
            printf("transform-case index=%u field=%u bits=%016llx sticky=%u origin=%u round=%u empty=%u",index++,field,static_cast<unsigned long long>(values[value]),sticky,origin,round,empty);emit("before",before);emit("after",after);printf("\n");emit_path(path);
        }
    }
    printf("transform-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
