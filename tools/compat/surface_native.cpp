// SPDX-License-Identifier: GPL-2.0-or-later
// Linked host-buffer operations, including overlap and signed/zero pitches.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include "xy_bitmap.h"
#include <cfenv>
#include <process.h>
static void exercise(unsigned index,unsigned kind,int w,int h,unsigned direction,unsigned position,unsigned alias,unsigned sticky){
    alignas(16) std::array<BYTE,3072> data;for(size_t i=0;i<data.size();++i)data[i]=BYTE(i*37+index*13+(i>>3));const auto initial=data;
    const bool planar=kind==1||kind==5;const int dw=w+2,dh=4;
    const int stride=((dw+15)&~15)*(planar?1:4),pitch=direction==0?stride:direction==1?-stride:0;
    const int bump=pitch<0?((planar?4:1)*dh-1)*stride:0;
    int source=1792+bump,destination=256+bump;
    if(alias==1)destination=source;
    if(alias==2)destination=source+(planar?1:4);
    if(alias==3)destination=source-(planar?1:4);
    if(alias==4)destination=source+stride;
    POINT pos={0,0};if(position==1){pos.x=-1;pos.y=-1;}if(position==2){pos.x=dw-1;pos.y=dh-1;}
    SIZE size={w,h};SubPicDesc spd;spd.type=planar?MSP_AYUV_PLANAR:MSP_RGB32;spd.w=dw;spd.h=dh;spd.bpp=planar?8:(index%3==0?24:32);spd.pitch=pitch;spd.bits=data.data()+destination;
    XyPlannerFormatExtra planes;for(int i=0;i<4;++i)planes.plans[i]=planar?data.data()+source+i*pitch*dh:nullptr;
    const unsigned alphas[]={0,1,127,128,255};const DWORD color=(alphas[index%5]<<24)|((index*0x91e10da5U)&0xffffff);
    for(unsigned stage=0;stage<2;++stage){
        const size_t memory=g_xy_malloc_used_size;reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);const Status before=status();
        switch(kind){
        case 0:XyBitmap::AlphaBltPack(spd,pos,size,data.data()+source,pitch);break;
        case 1:XyBitmap::AlphaBltPlannar(spd,pos,size,planes,pitch);break;
        case 2:XyBitmap::BltPack(spd,pos,size,data.data()+source,pitch);break;
        case 3:XyBitmap::FlipAlphaValue(spd.bits,w,h,pitch);break;
        default:Rasterizer::FillSolidRect(spd,pos.x,pos.y,w,h,color);break;
        }
        const Status after=status();printf("surface-stage index=%u stage=%u memory_delta=%lld",index,stage,static_cast<long long>(g_xy_malloc_used_size)-memory);emit("before",before);emit("after",after);
        // Lossless canonical delta against the same fully initialized input.
        // Every byte, including all outer/interior guards, participates.
        printf(" delta=");bool changed=false;
        for(size_t at=0;at<data.size();){if(data[at]==initial[at]){++at;continue;}size_t end=at+1;while(end<data.size()&&data[end]!=initial[end])++end;
            printf("%s%u:",changed?",":"",unsigned(at));for(size_t i=at;i<end;++i)printf("%02x",unsigned(data[i]));changed=true;at=end;}
        if(!changed)printf("-");printf("\n");check(g_xy_malloc_used_size==memory);
    }
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;CRenderedTextSubtitle renderer(&lock);unsigned index=0;
    printf("surface pointer_bits=%u\n",unsigned(sizeof(void*)*8));const int widths[]={1,3,4,7,8,15,16,17,31,32,33},heights[]={1,3};
    for(unsigned kind=0;kind<6;++kind)for(unsigned width=0;width<11;++width)for(unsigned height=0;height<2;++height)for(unsigned direction=0;direction<3;++direction)for(unsigned position=0;position<3;++position)for(unsigned alias=0;alias<5;++alias)for(unsigned sticky=0;sticky<2;++sticky){
        printf("surface-case index=%u kind=%u width=%u height=%u direction=%u position=%u alias=%u sticky=%u empty=0\n",index,kind,width,height,direction,position,alias,sticky);
        exercise(index++,kind,widths[width],heights[height],direction,position,alias,sticky);
    }
    for(unsigned kind=0;kind<6;++kind)for(unsigned shape=0;shape<3;++shape)for(unsigned sticky=0;sticky<2;++sticky){
        printf("surface-case index=%u kind=%u width=%u height=0 direction=0 position=0 alias=0 sticky=%u empty=1\n",index,kind,shape,sticky);
        exercise(index++,kind,shape==2?3:0,shape==1?3:0,0,0,0,sticky);
    }
    printf("surface-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
