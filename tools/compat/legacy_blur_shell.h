// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "blur_planes_shell.h"
static int legacyFilterWidth=3;
static uint64_t legacyFactories=0,legacyGaussCalls=0;
template<class T>struct GaussianFilterKey {};
struct ass_synth_priv {
    int g_r,g_w;unsigned gt2[2];
    explicit ass_synth_priv(double key):g_r(1),g_w(legacyFilterWidth){
        uint64_t bits;std::memcpy(&bits,&key,sizeof(bits));
        gt2[0]=static_cast<unsigned>(bits);gt2[1]=static_cast<unsigned>(bits>>32);
        coverageEvents.insert(coverageEvents.end(),{7,gt2[0],gt2[1],g_r,g_w});++legacyFactories;
    }
};
namespace boost {namespace flyweights {
struct no_locking {};
template<class K,class V,class Extractor>struct key_value {typedef V value_type;};
template<class Value,class Lock>class flyweight {
    typename Value::value_type value;
public:
    explicit flyweight(double key):value(key){}
    const typename Value::value_type& get()const{return value;}
};
}}
static void ass_gauss_blur(BYTE* p,unsigned* unused,int w,int h,int pitch,const unsigned* x,int rx,int wx,const unsigned* y,int ry,int wy){
    if(!unused)std::abort();++legacyGaussCalls;
    coverageEvents.insert(coverageEvents.end(),{8,rx,wx,ry,wy,x[0],x[1],y[0],y[1]});modeledFilter(p,w,h,pitch,9,0,0);
}
#define LEGACY_BLUR_CLASS class Rasterizer {typedef BYTE byte;public:static const float GAUSSIAN_BLUR_THREHOLD;static bool IsItReallyBlur(float,double);static bool OldFixedPointBlur(const Overlay&,float,double,double,double,SharedPtrOverlay);};

static void* checkedLegacyCopy(void* dst,const void* src,size_t count){
    bool destination=false,source=false;
    const uintptr_t d=reinterpret_cast<uintptr_t>(dst),s=reinterpret_cast<uintptr_t>(src);
    for(const auto& slot:coverageAllocations){const uintptr_t base=reinterpret_cast<uintptr_t>(slot.data());
        if(d>=base&&d-base<=slot.count&&count<=slot.count-(d-base))destination=true;
        if(s>=base&&s-base<=slot.count&&count<=slot.count-(s-base))source=true;
    }
    if(!destination||!source){fprintf(stderr,"Legacy copy outside preflight domain\n");std::abort();}
    return std::memcpy(dst,src,count);
}
