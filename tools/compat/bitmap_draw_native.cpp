// SPDX-License-Identifier: GPL-2.0-or-later
// Actual linked bitmap creation, drawing, final alpha, aliases and release.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include "xy_bitmap.h"
#include <cfenv>
#include <process.h>
static void seed_path(PathData& p,const std::vector<POINT>& points){
    std::vector<BYTE> types(points.size(),2);types[0]=6;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::PathSnapshot snapshot;snapshot.points=points;snapshot.types=types;p.Replace(snapshot);
#else
    p.mPathPoints=static_cast<int>(points.size());p.mpPathPoints=static_cast<POINT*>(malloc(points.size()*sizeof(POINT)));p.mpPathTypes=static_cast<BYTE*>(malloc(types.size()));
    check(p.mpPathPoints&&p.mpPathTypes);memcpy(p.mpPathPoints,points.data(),points.size()*sizeof(POINT));memcpy(p.mpPathTypes,types.data(),types.size());
#endif
}
static void show(unsigned index,unsigned stage,const XyBitmap* b,bool alias,const CRect& dirty,Status before,Status after,long long delta){
    printf("bitmap-stage index=%u stage=%u present=%u alias=%u dirty=%ld,%ld,%ld,%ld memory_delta=%lld",index,stage,unsigned(b!=nullptr),unsigned(alias),dirty.left,dirty.top,dirty.right,dirty.bottom,delta);emit("before",before);emit("after",after);
    if(b){
        check(b->bits!=nullptr&&b->plans[0]==b->bits);const bool planar=b->type==XyBitmap::PLANNA;
        printf(" fields=%d,%d,%d,%d,%d,%d residue=%u offsets=",b->type,b->x,b->y,b->w,b->h,b->pitch,unsigned(reinterpret_cast<uintptr_t>(b->bits)&15));
        for(unsigned i=0;i<4;++i)printf("%s%lld",i?",":"",b->plans[i]?static_cast<long long>(b->plans[i]-b->plans[0]):-1LL);
        const size_t count=planar?size_t(b->pitch)*b->h*4:size_t(b->w)*b->h*4;printf(" bytes=%llu pixels=",static_cast<unsigned long long>(count));
        if(planar){for(size_t i=0;i<count;++i)printf("%02x",unsigned(b->plans[0][i]));}
        else for(int y=0;y<b->h;++y)for(int x=0;x<b->w*4;++x)printf("%02x",unsigned(b->plans[0][y*b->pitch+x]));
    }printf("\n");
}
static void exercise(unsigned index,const SharedPtrOverlay& overlay,CRect target,CRect clip,int sx,int sy,unsigned layout,bool addition,const DWORD* switches,unsigned flags,bool sticky){
    const size_t baseline=g_xy_malloc_used_size;CRect dirty(17,29,31,37);reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);Status before=status();size_t memory=g_xy_malloc_used_size;
    std::shared_ptr<XyBitmap> bitmap(XyBitmap::CreateBitmap(target,static_cast<XyBitmap::MemLayout>(layout)));Status after=status();
    show(index,0,bitmap.get(),false,dirty,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
    reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);before=status();memory=g_xy_malloc_used_size;
    auto alpha=Rasterizer::CompositeAlphaMask(overlay,clip,nullptr,sx,sy,switches,(flags&1)!=0,(flags&2)!=0,&dirty);
    for(unsigned repeat=0;repeat<2;++repeat){
        if(repeat){reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);before=status();memory=g_xy_malloc_used_size;}
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        const auto& input=alpha;
#else
        auto input=alpha.get();
#endif
        // The retained DrawItem dispatch uses ordinary Draw for planar addition.
        if(addition&&layout==0)Rasterizer::AdditionDraw(bitmap.get(),overlay,dirty,input,sx,sy,switches,(flags&1)!=0,(flags&2)!=0);
        else Rasterizer::Draw(bitmap.get(),overlay,dirty,input,sx,sy,switches,(flags&1)!=0,(flags&2)!=0);
        after=status();show(index,repeat+1,bitmap.get(),false,dirty,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
    }
    reset();before=status();memory=g_xy_malloc_used_size;
    if(layout==0){
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        bitmap->FlipAlphaOwned();
#else
        XyBitmap::FlipAlphaValue(bitmap->bits,bitmap->w,bitmap->h,bitmap->pitch);
#endif
    }
    after=status();show(index,3,bitmap.get(),false,dirty,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
    reset();before=status();memory=g_xy_malloc_used_size;auto copy=bitmap;const bool alias=copy.get()==bitmap.get();bitmap.reset();decltype(alpha) empty;alpha=empty;
    after=status();show(index,4,copy.get(),alias,dirty,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
    reset();before=status();memory=g_xy_malloc_used_size;copy.reset();after=status();check(g_xy_malloc_used_size==baseline);
    show(index,5,nullptr,false,dirty,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;CRenderedTextSubtitle renderer(&lock);unsigned index=0;
    printf("bitmap-draw pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const int widths[]={1,3,15,16,17,31,33},origins[]={-3,0,5};
    const DWORD colors[][6]={{0,0,0,0,0,0},{0xff123456,0xffffffff,0,0,0,0},{0x80123456,0xffffffff,0,0,0,0},{0x80123456,0,0xffabcdef,6,0,0xffffffff},{0xff123456,0,0xffabcdef,6,0,0xffffffff},{0x80123456,0,0xffabcdef,0,0,0xffffffff},{0xff123456,3,0xffabcdef,8,0,0xffffffff}};
    for(unsigned width=0;width<7;++width){
        const long w=widths[width]*8;PathData path;seed_path(path,{{0,0},{w,0},{w,32},{0,32}});CPoint origin;CSize size;path.AlignLeftTop(&origin,&size);
        auto scan=std::make_shared<ScanLineData>();check(scan->ScanConvert(path,size));ScanLineData2 wide(origin,scan);check(wide.CreateWidenedRegion(8,8));auto overlay=std::make_shared<Overlay>();check(Rasterizer::Rasterize(wide,width&7,(width*3)&7,overlay));
        for(unsigned layout=0;layout<2;++layout)for(unsigned addition=0;addition<2;++addition)for(unsigned origin=0;origin<3;++origin)for(unsigned clip=0;clip<3;++clip)for(unsigned color=0;color<7;++color)for(unsigned sticky=0;sticky<2;++sticky){
            const unsigned flags=(width+color)%4;const int x=origins[origin];const CRect target(x,-2,x+widths[width]+8,6);
            const CRect clips[]={target,CRect(x+2,0,x+widths[width]-1,3),CRect(80,80,96,96)};
            printf("bitmap-case index=%u width=%u layout=%u addition=%u origin=%u clip=%u color=%u flags=%u sticky=%u empty=0\n",index,width,layout,addition,origin,clip,color,flags,sticky);
            exercise(index++,overlay,target,clips[clip],x*8,0,layout,addition!=0,color?colors[color]:nullptr,flags,sticky!=0);
        }
    }
    auto empty=std::make_shared<Overlay>();
    for(unsigned shape=0;shape<3;++shape)for(unsigned layout=0;layout<2;++layout)for(unsigned origin=0;origin<3;++origin){
        const int x=origins[origin];CRect target(x,2,x+(shape==2?3:0),2+(shape==1?3:0));
        printf("bitmap-case index=%u width=%u layout=%u addition=0 origin=%u clip=0 color=0 flags=0 sticky=0 empty=1\n",index,shape,layout,origin);
        exercise(index++,empty,target,target,0,0,layout,false,nullptr,0,false);
    }
    printf("bitmap-draw-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
