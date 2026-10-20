// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup raster metadata, all initialized pixels/padding, alias lifetime and cache accounting.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
static void seed_path(PathData& p,const std::vector<POINT>& points,const std::vector<BYTE>& types){
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points=points;snapshot.types=types;p.Replace(snapshot);
#else
    p.mPathPoints=static_cast<int>(points.size());p.mpPathPoints=static_cast<POINT*>(malloc(points.size()*sizeof(POINT)));p.mpPathTypes=static_cast<BYTE*>(malloc(types.size()));
    if(!points.empty()){check(p.mpPathPoints&&p.mpPathTypes);memcpy(p.mpPathPoints,points.data(),points.size()*sizeof(POINT));memcpy(p.mpPathTypes,types.data(),types.size());}
#endif
}
static void plane(const char* name,const SharedPtrByte& p,unsigned count){
    printf("raster-plane %s present=%u bytes=%u ",name,unsigned(bool(p)),p?count:0);
    if(p)for(unsigned i=0;i<count;++i)printf("%02x",unsigned(p.get()[i]));printf("\n");
}

static void show(const Overlay& overlay,unsigned index,const char* stage,unsigned shift,bool full,Status before,Status after,long long memory_delta){
    check(overlay.mOverlayWidth>0&&overlay.mOverlayHeight>0&&overlay.mOverlayPitch>=overlay.mOverlayWidth);
    check(int64_t(overlay.mOverlayPitch)*overlay.mOverlayHeight<1000000);
    printf("stage index=%u name=%s shift=%u offset=%d,%d size=%d,%d overlay=%d,%d,%d empty=%u full=%u memory_delta=%lld",index,stage,shift,overlay.mOffsetX,overlay.mOffsetY,overlay.mWidth,overlay.mHeight,overlay.mOverlayWidth,overlay.mOverlayHeight,overlay.mOverlayPitch,unsigned(overlay.mfWideOutlineEmpty),unsigned(full),memory_delta);emit("before",before);emit("after",after);printf("\n");
    for(unsigned i=0;i<2;++i){const auto& p=i?overlay.mBorder:overlay.mBody;printf("stage-plane index=%u present=%u ",i,unsigned(bool(p)));
        if(p)for(int y=0;y<overlay.mOverlayHeight;++y)for(int x=0;x<(full?overlay.mOverlayPitch:overlay.mOverlayWidth);++x)printf("%02x",unsigned(p.get()[y*overlay.mOverlayPitch+x]));printf("\n");}
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);unsigned index=0;
    printf("overlay-stages pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const int radii[][2]={{0,0},{1,0},{3,7},{15,19}};
    const float strengths[]={0.25f,1.0f,2.5f,4.0f};const double gaussian[]={0.1,0.333333,0.75,1.5};
    const float scales[][2]={{0.5f,1.0f},{1.0f,1.0f},{1.75f,0.5f},{2.0f,1.25f}};
    const unsigned shifts[][2]={{0,0},{1,7},{7,1},{UINT_MAX,0}};
    auto scan=std::make_shared<ScanLineData>();auto raster=std::make_shared<Overlay>();auto output=std::make_shared<Overlay>();
    for(unsigned scenario=0;scenario<8;++scenario){
        const long w=65+scenario*17,h=57+scenario*13;
        std::vector<POINT> points={{-13,7},{w,11},{w-17,h},{-3,h-11}};std::vector<BYTE> types={6,2,2,2};
        PathData path;seed_path(path,points,types);CPoint origin;CSize size;path.AlignLeftTop(&origin,&size);check(scan->ScanConvert(path,size));ScanLineData2 wide(origin,scan);
        for(unsigned radius=0;radius<4;++radius){check(wide.CreateWidenedRegion(radii[radius][0],radii[radius][1]));check(Rasterizer::Rasterize(wide,scenario&7,(scenario*3)&7,raster));
            for(unsigned settings=0;settings<4;++settings){
                for(unsigned stage=0;stage<4;++stage){
                    const float sx=scales[settings][0],sy=scales[settings][1];const size_t before_memory=g_xy_malloc_used_size;reset();if(settings%2)std::feraiseexcept(FE_DIVBYZERO);Status before=status();bool result=true;
                    if(stage==1)result=Rasterizer::BeBlur(*raster,strengths[settings],sx,sy,output);
                    if(stage==2)result=Rasterizer::GaussianBlur(*raster,gaussian[settings],sx,sy,output);
                    if(stage==3)result=Rasterizer::Blur(*raster,strengths[settings],gaussian[settings],sx,sy,output);
                    Status after=status();check(result);auto current=stage?output:raster;const char* name=stage==0?"raster":stage==1?"be":stage==2?"gaussian":"combined";
                    show(*current,index++,name,4,stage!=2,before,after,static_cast<long long>(g_xy_malloc_used_size)-before_memory);
                    const int old_x=current->mOffsetX,old_y=current->mOffsetY;
                    const size_t copy_memory=g_xy_malloc_used_size;reset();before=status();
                    auto copied=std::make_shared<Overlay>();*copied=*current;*copied=*copied;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                    copied->SetOffsets(old_x+17,old_y-23);
#else
                    copied->mOffsetX=old_x+17;copied->mOffsetY=old_y-23;
#endif
                    after=status();check(current->mOffsetX==old_x&&current->mOffsetY==old_y);
                    check(copied->mBody==current->mBody&&copied->mBorder==current->mBorder);
                    show(*copied,index++,name,9,stage!=2,before,after,static_cast<long long>(g_xy_malloc_used_size)-copy_memory);
                    current=copied;
                    for(unsigned shift=0;shift<4;++shift){const size_t variance_memory=g_xy_malloc_used_size;reset();if(shift%2)std::feraiseexcept(FE_DIVBYZERO);before=status();
                        std::unique_ptr<Overlay> variance(current->GetSubpixelVariance(shifts[shift][0],shifts[shift][1]));after=status();check(bool(variance));
                        const bool copies_padding=variance->mOverlayPitch==current->mOverlayPitch&&variance->mOverlayWidth==current->mOverlayWidth&&variance->mOverlayHeight>=current->mOverlayHeight;
                        show(*variance,index++,name,shift,stage!=2||!copies_padding,before,after,static_cast<long long>(g_xy_malloc_used_size)-variance_memory);
                        const size_t chain_memory=g_xy_malloc_used_size;reset();before=status();
                        std::unique_ptr<Overlay> chained(variance->GetSubpixelVariance(7,7));after=status();check(bool(chained));
                        const bool chain_copies=chained->mOverlayPitch==variance->mOverlayPitch&&chained->mOverlayWidth==variance->mOverlayWidth&&chained->mOverlayHeight>=variance->mOverlayHeight;
                        show(*chained,index++,name,shift+5,stage!=2||!copies_padding||!chain_copies,before,after,static_cast<long long>(g_xy_malloc_used_size)-chain_memory);
                    }
                }
            }
        }
    }
    Overlay empty;
    for(unsigned shift=0;shift<4;++shift){const size_t memory=g_xy_malloc_used_size;reset();const Status before=status();
        std::unique_ptr<Overlay> result(empty.GetSubpixelVariance(shifts[shift][0],shifts[shift][1]));const Status after=status();check(bool(result));
        show(*result,index++,"empty",shift,true,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);}
    printf("overlay-stages-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
