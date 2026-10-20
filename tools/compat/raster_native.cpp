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
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    auto scan=std::make_shared<ScanLineData>();auto overlay=std::make_shared<Overlay>();unsigned index=0;
    printf("raster pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const int radii[][2]={{-3,-5},{0,0},{1,0},{3,7},{15,19},{64,64},{65,65},{128,3}};
    const int shifts[][2]={{0,0},{1,7},{-1,-9},{INT_MIN,INT_MAX}};
    for(unsigned scenario=0;scenario<16;++scenario){
        std::vector<POINT> points;std::vector<BYTE> types;uint32_t rng=0x4b19de37u+scenario;
        for(unsigned i=0;i<8;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            points.push_back(POINT{static_cast<long>(rng%256)-128,static_cast<long>((rng>>12)%256)});types.push_back(i?2:6);}
        if(scenario%8==1){types[3]=6;types[6]=6;}
        if(scenario%8==2){types[3]=0xfe;types[5]=0xff;}
        if(scenario%8==3){types[1]=types[2]=types[3]=4;types[4]=types[5]=types[6]=4;}
        if(scenario%8==4){types[1]=types[2]=types[3]=0xfc;types[4]=types[5]=types[6]=0xfc;}
        if(scenario%8==5){types[1]=types[2]=types[3]=0xfc;types[4]=0xfa;}
        if(scenario%8==6)for(auto& t:types)t|=1;
        if(scenario%8==7)for(auto& p:points)p.y=16;
        if(scenario==15){points.clear();types.clear();}
        PathData path;seed_path(path,points,types);CPoint origin;CSize size;path.AlignLeftTop(&origin,&size);scan->ScanConvert(path,size);
        ScanLineData2 wide(origin,scan);
        for(unsigned radius=0;radius<8;++radius){wide.CreateWidenedRegion(radii[radius][0],radii[radius][1]);
            for(unsigned shift=0;shift<4;++shift){
                SharedPtrByte retained=shift%2?overlay->mBody:SharedPtrByte();
                const unsigned retained_count=unsigned(overlay->mOverlayPitch*overlay->mOverlayHeight);
                const size_t memory_before=g_xy_malloc_used_size;
                reset();if(shift%2)std::feraiseexcept(FE_DIVBYZERO);const Status before=status();
                const bool missing=Rasterizer::Rasterize(wide,shifts[shift][0],shifts[shift][1],SharedPtrOverlay());
                const bool result=Rasterizer::Rasterize(wide,shifts[shift][0],shifts[shift][1],overlay);const Status after=status();const long long memory_delta=static_cast<long long>(g_xy_malloc_used_size)-static_cast<long long>(memory_before);
                check(overlay->mOverlayPitch>=0&&overlay->mOverlayHeight>=0&&int64_t(overlay->mOverlayPitch)*overlay->mOverlayHeight<=1000000);
                const unsigned bytes=unsigned(overlay->mOverlayPitch*overlay->mOverlayHeight);
                printf("raster-case index=%u scenario=%u radius=%u shift=%u missing=%u result=%u origin=%ld,%ld scan=%ld,%ld offset=%d,%d size=%d,%d overlay=%d,%d,%d wide_empty=%u memory_delta=%lld",index++,scenario,radius,shift,unsigned(missing),unsigned(result),origin.x,origin.y,size.cx,size.cy,overlay->mOffsetX,overlay->mOffsetY,overlay->mWidth,overlay->mHeight,overlay->mOverlayWidth,overlay->mOverlayHeight,overlay->mOverlayPitch,unsigned(overlay->mfWideOutlineEmpty),memory_delta);emit("before",before);emit("after",after);printf("\n");
                plane("body",overlay->mBody,bytes);plane("border",overlay->mBorder,bytes);plane("retained",retained,retained_count);
            }
        }
    }
    printf("raster-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
