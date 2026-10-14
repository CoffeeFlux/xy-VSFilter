// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "blur_planes_shell.h"
struct PlaneInitialization {BYTE* data;size_t count;std::vector<bool> written;};
static std::vector<PlaneInitialization> planeInitialization;
static uint64_t initializedComparisons=0,unwrittenComparisons=0,gaussPlaneFactories=0,gaussPlaneKernels=0;
static unsigned factoryAxis=0;
static int factoryRadii[2]={0,0};
static void registerInitialization(BYTE* data,size_t count,bool initialized){
    // Allocation addresses can be recycled during output reuse. Retired masks
    // must not shadow a new, smaller allocation within the old address range.
    planeInitialization.erase(std::remove_if(planeInitialization.begin(),planeInitialization.end(),
        [](const PlaneInitialization& region){
            for(const auto& allocation:coverageAllocations)
                if(allocation.data()==region.data&&allocation.count==region.count)return false;
            return true;
        }),planeInitialization.end());
    for(size_t i=0;i<planeInitialization.size();++i)if(planeInitialization[i].data==data){planeInitialization.erase(planeInitialization.begin()+i);break;}
    planeInitialization.push_back({data,count,std::vector<bool>(count,initialized)});
}
static PlaneInitialization& initializedRegion(const void* data,size_t count,size_t& offset){
    const uintptr_t p=reinterpret_cast<uintptr_t>(data);
    for(auto& region:planeInitialization){const uintptr_t b=reinterpret_cast<uintptr_t>(region.data);
        if(p>=b&&p-b<=region.count&&count<=region.count-(p-b)){offset=static_cast<size_t>(p-b);return region;}
    }
    fprintf(stderr,"Gaussian plane callback outside modeled extent\n");std::abort();
}
static void* gaussPlaneAllocate(size_t count){
    void* p=xy_malloc(count);if(p)registerInitialization(static_cast<BYTE*>(p),count,false);return p;
}
namespace XyRust {
class ObservedGaussPlaneAllocation:public PlaneAllocation {
public:
    explicit ObservedGaussPlaneAllocation(size_t count):PlaneAllocation(count){if(Ready())registerInitialization(Data(),count,false);}
};
}
static void* gaussPlaneSet(void* p,int value,size_t count){
    size_t offset;auto& region=initializedRegion(p,count,offset);
    coverageEvents.insert(coverageEvents.end(),{10,value,static_cast<int64_t>(count)});
    std::memset(p,value,count);for(size_t i=0;i<count;++i)region.written[offset+i]=true;return p;
}
static void* gaussPlaneCopy(void* dst,const void* src,size_t count){
    size_t a,b;auto& destination=initializedRegion(dst,count,a);const auto& source=initializedRegion(src,count,b);
    for(size_t i=0;i<count;++i)if(!source.written[b+i]){fprintf(stderr,"Uninitialized modeled Gaussian source\n");std::abort();}
    coverageEvents.insert(coverageEvents.end(),{11,static_cast<int64_t>(count)});
    std::memcpy(dst,src,count);for(size_t i=0;i<count;++i)destination.written[a+i]=true;return dst;
}
static BYTE readInitialized(const BYTE* p){size_t offset;const auto& region=initializedRegion(p,1,offset);if(!region.written[offset])std::abort();return *p;}
static std::vector<uint64_t> gaussPlaneSnapshot(const Overlay& overlay,bool count=false){
    std::vector<uint64_t> result={static_cast<uint64_t>(overlay.mOffsetX),static_cast<uint64_t>(overlay.mOffsetY),static_cast<uint64_t>(overlay.mWidth),static_cast<uint64_t>(overlay.mHeight),static_cast<uint64_t>(overlay.mOverlayWidth),static_cast<uint64_t>(overlay.mOverlayHeight),static_cast<uint64_t>(overlay.mOverlayPitch),overlay.mfWideOutlineEmpty?1u:0u};
    for(const auto& plane:{overlay.mBody,overlay.mBorder}){
        if(!plane){result.push_back(0);continue;}
        size_t offset;const auto& region=initializedRegion(plane.get(),1,offset);if(offset)std::abort();result.push_back(region.count);
        for(size_t i=0;i<region.count;++i){result.push_back(region.written[i]?1:0);if(region.written[i]){result.push_back(plane.get()[i]);initializedComparisons+=count;}else unwrittenComparisons+=count;}
        for(const auto& allocation:coverageAllocations)if(allocation.data()==plane.get())checkCoverageGuards(allocation);
    }
    return result;
}
template<class T>struct GaussianFilterKey {};
struct GaussianCoefficients {
    int g_r,g_w,g_w_ex;std::vector<float> storage;const float* g_f;
    explicit GaussianCoefficients(double key){
        if(factoryAxis>=2)std::abort();g_r=factoryRadii[factoryAxis++];g_w=2*g_r+1;g_w_ex=(g_w+3)&~3;
        storage.assign(g_w_ex,0.25f);g_f=storage.data();uint64_t bits;std::memcpy(&bits,&key,8);
        coverageEvents.insert(coverageEvents.end(),{7,static_cast<int64_t>(bits),g_r,g_w,g_w_ex});++gaussPlaneFactories;
    }
};
namespace boost {namespace flyweights {
struct no_locking {};
template<class K,class V,class E>struct key_value {typedef V value_type;};
template<class Value,class Lock>class flyweight {
    typename Value::value_type value;
public:
    explicit flyweight(double key):value(key){}
    const typename Value::value_type& get()const{return value;}
};
}}
static void xy_gaussian_blur(BYTE* dst,int ds,const BYTE* src,int w,int h,int ss,const float* x,int rx,int wx,const float* y,int ry,int wy){
    ++gaussPlaneKernels;coverageEvents.insert(coverageEvents.end(),{8,ds,w,h,ss,rx,wx,ry,wy,floatBits(x[0]),floatBits(y[0])});
    for(int row=0;row<h+2*ry;++row)for(int col=0;col<w+2*rx;++col){
        const BYTE input=readInitialized(src+(row%h)*ss+(col%w));BYTE* target=dst+row*ds+col;
        size_t offset;auto& region=initializedRegion(target,1,offset);*target=static_cast<BYTE>(input+row*3+col*7);region.written[offset]=true;
    }
}
#define GAUSS_PLANES_CLASS class Rasterizer {typedef BYTE byte;public:static const float GAUSSIAN_BLUR_THREHOLD;static bool GaussianBlur(const Overlay&,double,double,double,SharedPtrOverlay);};
