// SPDX-License-Identifier: GPL-2.0-or-later
// Actual private cleanup clipper method, including repeated paints and aliases.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <cache_manager.h>
#include <cfenv>
#include <process.h>
struct PaintTag {typedef GrayImage2*(CClipper::*type)();friend type method(PaintTag);};
struct PolygonTag {typedef SharedPtrCPolygon CClipper::*type;friend type method(PolygonTag);};
template<class Tag,typename Tag::type Member>struct Access {friend typename Tag::type method(Tag){return Member;}};
template struct Access<PaintTag,&CClipper::Paint>;
template struct Access<PolygonTag,&CClipper::m_polygon>;
static void show(unsigned index,unsigned stage,const GrayImage2* image,bool alias,Status before,Status after,long long delta){
    printf("clip-stage index=%u stage=%u present=%u alias=%u memory_delta=%lld",index,stage,unsigned(image!=nullptr),unsigned(alias),delta);emit("before",before);emit("after",after);
    if(image){check(image->size.cx>=0&&image->size.cy>=0&&image->pitch>=image->size.cx);check(int64_t(image->pitch)*image->size.cy<1000000);
        printf(" position=%ld,%ld size=%ld,%ld pitch=%d data=%u pixels=",image->left_top.x,image->left_top.y,image->size.cx,image->size.cy,image->pitch,unsigned(bool(image->data)));
        if(image->data)for(int y=0;y<image->size.cy;++y)for(int x=0;x<image->pitch;++x)printf("%02x",unsigned(image->data.get()[y*image->pitch+x]));
    }printf("\n");
}
static void observe(unsigned index,CClipper& clip,unsigned sticky){
    PaintTag::type volatile paint=method(PaintTag{});std::unique_ptr<GrayImage2> images[2];
    for(unsigned stage=0;stage<2;++stage){const size_t memory=g_xy_malloc_used_size;reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);Status before=status();
        images[stage].reset((clip.*paint)());Status after=status();const bool alias=stage&&images[0]&&images[1]&&images[0]->data==images[1]->data;
        show(index,stage,images[stage].get(),alias,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);}
    const size_t memory=g_xy_malloc_used_size;reset();Status before=status();std::unique_ptr<GrayImage2> copy;
    if(images[0])copy.reset(new GrayImage2(*images[0]));bool alias=copy&&copy->data==images[0]->data;images[0].reset();images[1].reset();Status after=status();
    show(index,2,copy.get(),alias,before,after,static_cast<long long>(g_xy_malloc_used_size)-memory);
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;CRenderedTextSubtitle renderer(&lock);unsigned index=0;
    const wchar_t* shapes[]={L"m 0 0 l 13 0 13 9 0 9",L"m -7 -3 l 11 2 -4 13 8 -2",L"m 80 80 l 90 80 90 90 80 90",L"m 0 0"};
    const CSize sizes[]={CSize(0,0),CSize(-1,8),CSize(8,0),CSize(1,1),CSize(7,5),CSize(19,11),CSize(40,24)};
    const int effects[]={-1,3,4,0,99};const double scales[][2]={{1,1},{0.5,1.25},{1.75,0.5}};
    const int settings[][3]={{4,-8,40},{12,8,0},{-4,80,16}};
    printf("clipper pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    for(unsigned shape=0;shape<4;++shape)for(unsigned size=0;size<7;++size)for(unsigned effect=0;effect<5;++effect)for(unsigned inverse=0;inverse<2;++inverse)for(unsigned scale=0;scale<3;++scale)for(unsigned setting=0;setting<3;++setting)for(unsigned sticky=0;sticky<2;++sticky){
        CClipper clip(shapes[shape],sizes[size],1,1,inverse!=0,scales[scale][0],scales[scale][1]);Effect e;e.type=EF_BANNER;e.param[0]=settings[setting][1];e.param[1]=settings[setting][2];e.param[2]=e.param[4]=settings[setting][0];clip.SetEffect(e,effects[effect]);
        printf("clip-case index=%u raw=0 shape=%u size=%u effect=%d inverse=%u scale=%u setting=%u sticky=%u\n",index,shape,size,effects[effect],inverse,scale,setting,sticky);observe(index++,clip,sticky);
    }
    const uint64_t raw[]={0,0x8000000000000000ull,1,0x8000000000000001ull,0x3ff0000000000000ull,0xbff0000000000000ull,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000000000042ull,0xfff8000000000042ull,0x7ff0000000000042ull,0xfff0000000000042ull,0x7fefffffffffffffull,0x3ff0000000000001ull};
    for(unsigned scale=0;scale<14;++scale)for(unsigned effect=0;effect<4;++effect)for(unsigned sticky=0;sticky<2;++sticky){
        CClipper clip(shapes[0],CSize(-1,8),1,1,false);Effect e;e.type=EF_BANNER;e.param[2]=INT_MAX;e.param[4]=INT_MIN;clip.SetEffect(e,effects[effect]);
        auto polygon=clip.*method(PolygonTag{});memcpy(&polygon->m_target_scale_x,&raw[scale],8);memcpy(&polygon->m_target_scale_y,&raw[scale],8);
        printf("clip-case index=%u raw=1 shape=0 size=1 effect=%d inverse=0 scale=%u setting=3 sticky=%u\n",index,effects[effect],scale,sticky);observe(index++,clip,sticky);
    }
    printf("clipper-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
