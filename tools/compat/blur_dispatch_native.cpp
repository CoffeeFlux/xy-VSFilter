// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup predicates/early returns, including nonfinite scalars.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
#include "xy_bitmap.h"
static unsigned index=0;
static void observe(const Overlay& source,SharedPtrOverlay output,uint32_t b,uint64_t g,unsigned sticky,bool positive){
    float be;double gaussian;memcpy(&be,&b,4);memcpy(&gaussian,&g,8);const auto memory=g_xy_malloc_used_size;
    reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const auto before=status();const bool active=Rasterizer::IsItReallyBlur(be,gaussian);const auto predicate=status();
    reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const bool result=Rasterizer::Blur(source,be,gaussian,1,1,output);const auto after=status();
    reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const bool missing=Rasterizer::Blur(source,be,gaussian,1,1,SharedPtrOverlay());const auto null_status=status();
    const bool preserved=output->mOffsetX==71&&output->mOffsetY==-29&&!output->mBody&&!output->mBorder&&output->mOverlayWidth==0&&output->mOverlayHeight==0;
    printf("dispatch index=%u positive=%u be=%08x gaussian=%016llx sticky=%u active=%u result=%u null=%u preserved=%u memory_delta=%lld",index++,unsigned(positive),b,g,sticky,unsigned(active),unsigned(result),unsigned(missing),unsigned(preserved),static_cast<long long>(g_xy_malloc_used_size)-memory);
    emit("before",before);emit("predicate",predicate);emit("after",after);emit("missing",null_status);printf("\n");check(preserved);
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    const uint32_t be[]={0,0x80000000,1,0x80000001,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00001,0xffc00001,0x7f800001,0xff800001};
    const uint64_t gauss[]={0,0x8000000000000000ULL,1,0x8000000000000001ULL,0x3fd55553dfffffffULL,0x3fd55553e0000000ULL,0x3fd55553e0000001ULL,0x3fb999999999999aULL,0x3fe8000000000000ULL,0xbff0000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000001ULL,0xfff8000000000001ULL,0x7ff0000000000001ULL,0xfff0000000000001ULL};
    printf("blur-dispatch pointer_bits=%u be_inputs=12 gaussian_inputs=16\n",unsigned(sizeof(void*)*8));
    Overlay empty;auto output=std::make_shared<Overlay>();
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    output->SetOffsets(71,-29);
#else
    output->mOffsetX=71;output->mOffsetY=-29;
#endif
    for(auto b:be)for(auto g:gauss)for(unsigned sticky=0;sticky<2;++sticky)observe(empty,output,b,g,sticky,false);
    PathData path;const std::vector<POINT> points={{0,0},{64,0},{64,64},{0,64}};const std::vector<BYTE> types={6,2,2,2};
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points=points;snapshot.types=types;path.Replace(snapshot);
#else
    path.mPathPoints=4;path.mpPathPoints=static_cast<POINT*>(malloc(4*sizeof(POINT)));path.mpPathTypes=static_cast<BYTE*>(malloc(4));check(path.mpPathPoints&&path.mpPathTypes);memcpy(path.mpPathPoints,points.data(),4*sizeof(POINT));memcpy(path.mpPathTypes,types.data(),4);
#endif
    CPoint origin;CSize size;path.AlignLeftTop(&origin,&size);auto scan=std::make_shared<ScanLineData>();check(scan->ScanConvert(path,size));ScanLineData2 wide(origin,scan);check(wide.CreateWidenedRegion(0,0));auto source=std::make_shared<Overlay>();check(Rasterizer::Rasterize(wide,0,0,source));
    for(unsigned z=0;z<2;++z)for(unsigned g=12;g<16;++g)for(unsigned sticky=0;sticky<2;++sticky)observe(*source,output,be[z],gauss[g],sticky,true);
    printf("blur-dispatch-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
