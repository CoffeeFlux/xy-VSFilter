// SPDX-License-Identifier: GPL-2.0-or-later
// Calls the actual cleanup dispatcher; candidate uses the production safe filter.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
#include "xy_bitmap.h"
void xy_filter_sse(float*,int,int,int,const float*,int);
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
extern "C" uint32_t __cdecl xy_vsfilter_gaussian_filter_v1(const uint8_t*,size_t,uint8_t*,size_t);
#endif
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    const unsigned widths[]={4,8,12,16,20,24,28,32,36,48,64,128};
    const unsigned masks[]={4,8,12,16,20,24,28,32,36,64};unsigned index=0;
    printf("gaussian-filter pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    for(unsigned w:widths)for(unsigned m:masks)for(unsigned trim=0;trim<4;++trim)for(unsigned pattern=0;pattern<4;++pattern)for(unsigned sticky=0;sticky<2;++sticky){
        const unsigned height=3,stride=w+m+4,count=stride*height;
        auto* data=reinterpret_cast<uint32_t*>(xy_malloc(count*4));auto* filter=reinterpret_cast<uint32_t*>(xy_malloc(m*4));check(data&&filter);
        for(unsigned i=0;i<count;++i)data[i]=0x7fc01234;
        for(unsigned i=0;i<m;++i){float f=float(1+i%5)/16;memcpy(filter+i,&f,4);if(pattern==3)filter[i]=1+i%5;}
        for(unsigned i=m-trim;i<m;++i)filter[i]=0;
        if(m==4&&pattern==2)filter[2]=filter[0];
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){
            float v=float((x*37+y*13)%257)/8;uint32_t bits;memcpy(&bits,&v,4);
            if(pattern==1&&x%3)bits|=0x80000000;
            if(pattern==2)bits=(x%3)?(1+x+y*7):0x80000000;
            data[y*stride+m+x]=bits;
        }
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        using namespace XyRust::DocumentDetail;Bytes packet(16+4*(m+count));
        Put(packet,0,w,4);Put(packet,4,height,4);Put(packet,8,stride,4);Put(packet,12,m,4);
        memcpy(packet.data()+16,filter,m*4);memcpy(packet.data()+16+m*4,data,count*4);
#endif
        reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const auto before=status();
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        Check(Invoke([&](){return xy_vsfilter_gaussian_filter_v1(packet.data(),packet.size(),reinterpret_cast<uint8_t*>(data),count*4);}));
#else
        xy_filter_sse(reinterpret_cast<float*>(data+m),w,height,stride*4,reinterpret_cast<float*>(filter),m);
#endif
        const auto after=status();
        printf("filter index=%u width=%u mask=%u trim=%u pattern=%u sticky=%u",index++,w,m,trim,pattern,sticky);emit("before",before);emit("after",after);printf("\nvalues ");
        for(unsigned i=0;i<count;++i)printf("%08x",data[i]);printf("\n");xy_free(data);xy_free(filter);
    }
    printf("gaussian-filter-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
