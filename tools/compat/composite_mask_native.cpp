// SPDX-License-Identifier: GPL-2.0-or-later
// Actual linked compositor, initialized footprint bytes and surviving aliases.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <cfenv>
#include <process.h>
struct PaintTag {typedef GrayImage2*(CClipper::*type)();friend type method(PaintTag);};
template<class Tag,typename Tag::type Member>struct Access {friend typename Tag::type method(Tag){return Member;}};
template struct Access<PaintTag,&CClipper::Paint>;
static void seed_path(PathData& p,const std::vector<POINT>& points){
    std::vector<BYTE> types(points.size(),2);types[0]=6;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points=points;snapshot.types=types;p.Replace(snapshot);
#else
    p.mPathPoints=static_cast<int>(points.size());p.mpPathPoints=static_cast<POINT*>(malloc(points.size()*sizeof(POINT)));p.mpPathTypes=static_cast<BYTE*>(malloc(types.size()));
    check(p.mpPathPoints&&p.mpPathTypes);memcpy(p.mpPathPoints,points.data(),points.size()*sizeof(POINT));memcpy(p.mpPathTypes,types.data(),types.size());
#endif
}
static void show(unsigned index,unsigned stage,const BYTE* data,bool alias,const CRect& dirty,const Overlay& source,int sx,int sy,unsigned color,Status before,Status after,long long delta){
    printf("composite-stage index=%u stage=%u present=%u alias=%u dirty=%ld,%ld,%ld,%ld memory_delta=%lld",index,stage,unsigned(data!=nullptr),unsigned(alias),dirty.left,dirty.top,dirty.right,dirty.bottom,delta);emit("before",before);emit("after",after);
    if(data){
        const int ox=(sx+source.mOffsetX+4)>>3,oy=(sy+source.mOffsetY+4)>>3;
        int first=dirty.left-ox;const int end=dirty.right-ox,top=dirty.top-oy,height=dirty.Height();
        // These bounded two-color fixtures may initialize an earlier prefix.
        // Equal alpha coalesces; different alpha retains the earlier boundary.
        if(color==3)first=min(first,6);if(color==5)first=0;
        check(first>=0&&end>first&&end<=source.mOverlayWidth&&top>=0&&height>0&&top+height<=source.mOverlayHeight);
        printf(" origin=%d,%d size=%d,%d pitch=%d pixels=",first,top,end-first,height,source.mOverlayPitch);
        for(int y=top;y<top+height;++y)for(int x=first;x<end;++x)printf("%02x",unsigned(data[y*source.mOverlayPitch+x]));
    }printf("\n");
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;CRenderedTextSubtitle renderer(&lock);unsigned index=0;
    printf("composite-mask pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    std::unique_ptr<GrayImage2> masks[4];PaintTag::type volatile paint=method(PaintTag{});
    for(unsigned mode=1;mode<4;++mode){
        CClipper clip(mode==1?L"m -16 -16 l 48 -16 -16 48":mode==2?L"m 0 0 l 64 0 64 64 0 64":L"m 2 2 l 8 2 8 8 2 8",CSize(64,64),1,1,mode==3);
        if(mode>=2){Effect e;e.type=EF_BANNER;e.param[0]=-8;e.param[1]=80;e.param[2]=12;e.param[4]=8;clip.SetEffect(e,mode==2?EF_BANNER:EF_SCROLL);}
        masks[mode].reset((clip.*paint)());check(bool(masks[mode]));
    }
    const CRect clips[]={CRect(-32,-32,64,64),CRect(0,0,5,7),CRect(80,80,96,96),CRect(0,0,0,0)};
    const int subs[][2]={{0,0},{7,-7},{-9,13}};
    const DWORD colors[][6]={{0,0,0,0,0,0},{0xff123456,0xffffffff,0,0,0,0},{0x80123456,0xffffffff,0,0,0,0},{0x80123456,0,0xffabcdef,6,0,0xffffffff},{0xff123456,0,0xffabcdef,6,0,0xffffffff},{0x80123456,0,0xffabcdef,0,0,0xffffffff}};
    for(unsigned shape=0;shape<3;++shape){
        const long w=65+shape*17,h=57+shape*13;std::vector<POINT> points={{-13,7},{w,11},{w-17,h},{-3,h-11}};
        PathData path;seed_path(path,points);CPoint origin;CSize size;path.AlignLeftTop(&origin,&size);auto scan=std::make_shared<ScanLineData>();check(scan->ScanConvert(path,size));ScanLineData2 wide(origin,scan);
        for(unsigned border=0;border<2;++border){
            check(wide.CreateWidenedRegion(border?15:0,border?19:0));auto raster=std::make_shared<Overlay>();check(Rasterizer::Rasterize(wide,shape&7,(shape*3)&7,raster));
            for(unsigned clip=0;clip<4;++clip)for(unsigned sub=0;sub<3;++sub)for(unsigned mask=0;mask<4;++mask)for(unsigned color=0;color<6;++color)for(unsigned flags=0;flags<4;++flags)for(unsigned sticky=0;sticky<2;++sticky){
                printf("composite-case index=%u shape=%u border=%u clip=%u sub=%u mask=%u color=%u flags=%u sticky=%u\n",index,shape,border,clip,sub,mask,color,flags,sticky);
                const size_t memory=g_xy_malloc_used_size;CRect dirty(17,29,31,37);reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);Status before=status();
                auto result=Rasterizer::CompositeAlphaMask(raster,clips[clip],masks[mask].get(),subs[sub][0],subs[sub][1],color?colors[color]:nullptr,(flags&1)!=0,(flags&2)!=0,&dirty);Status after=status();
                show(index,0,result.get(),false,dirty,*raster,subs[sub][0],subs[sub][1],color,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
                const size_t copied_memory=g_xy_malloc_used_size;reset();before=status();auto copy=result;const bool alias=copy.get()!=nullptr&&copy.get()==result.get();decltype(result) empty;result=empty;after=status();
                show(index,1,copy.get(),alias,dirty,*raster,subs[sub][0],subs[sub][1],color,before,after,static_cast<long long>(g_xy_malloc_used_size)-copied_memory);
                const size_t released_memory=g_xy_malloc_used_size;reset();before=status();copy=empty;after=status();check(g_xy_malloc_used_size==memory);
                show(index++,2,nullptr,false,dirty,*raster,subs[sub][0],subs[sub][1],color,before,after,static_cast<long long>(g_xy_malloc_used_size)-released_memory);
            }
        }
    }
    printf("composite-mask-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
