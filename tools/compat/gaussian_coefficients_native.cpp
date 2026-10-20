// SPDX-License-Identifier: GPL-2.0-or-later
// Calls the cleanup's linked private init method, not a copied numeric expression.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
#include "xy_bitmap.h"
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include "RustGaussianCoefficients.h"
using GaussianCoefficients=XyRust::GaussianCoefficients;
#else
class GaussianCoefficients {
public:
    int g_r,g_w,g_w_ex;float* g_f;double sigma;
    explicit GaussianCoefficients(double value):g_r(0),g_w(0),g_w_ex(0),g_f(nullptr),sigma(0){init(value);}
    GaussianCoefficients(const GaussianCoefficients& value):g_r(value.g_r),g_w(value.g_w),g_w_ex(value.g_w_ex),g_f(nullptr),sigma(value.sigma){
        if(g_w_ex>0){g_f=reinterpret_cast<float*>(xy_malloc(size_t(g_w_ex)*4));check(g_f!=nullptr);memcpy(g_f,value.g_f,size_t(g_w_ex)*4);}}
    ~GaussianCoefficients(){xy_free(g_f);}
private:
    int init(double); // Resolved from pinned subtitlesR.lib, on both architectures.
};
#endif
static uint64_t bits(double value){uint64_t out;memcpy(&out,&value,8);return out;}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    std::vector<uint64_t> inputs={0,0x8000000000000000ULL,1,0x000fffffffffffffULL,
        0xbfb999999999999aULL,0xbff0000000000000ULL,0xc010000000000000ULL,
        0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000001ULL,
        0xfff8000000000001ULL,0x7ff0000000000001ULL,0xfff0000000000001ULL,0x7fefffffffffffffULL};
    const double values[]={0.000001,0.1,0.333333,0.33333333333333331,0.5,1,1.5,2,4,8,16,32,128};
    for(double value:values){const auto v=bits(value);inputs.push_back(v-1);inputs.push_back(v);inputs.push_back(v+1);}
    unsigned index=0;using Fw=XyInternedByKey<double,GaussianCoefficients>;
    printf("gaussian-coefficients pointer_bits=%u inputs=%u\n",unsigned(sizeof(void*)*8),unsigned(inputs.size()));
    for(unsigned repeat=0;repeat<2;++repeat)for(unsigned sticky=0;sticky<2;++sticky)for(auto input:inputs){
        double value;memcpy(&value,&input,8);const size_t memory=g_xy_malloc_used_size;
        reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const auto before=status();
        auto first=std::make_unique<Fw>(value);const auto after_first=status();
        auto second=std::make_unique<Fw>(value);const auto after_second=status();
        auto copied=std::make_unique<GaussianCoefficients>(first->get());const auto after_copy=status();
        const auto& c=first->get();check(c.g_w_ex<4096);const size_t count=c.g_f?size_t(c.g_w_ex):0;
        check(!count||memcmp(c.g_f,copied->g_f,count*4)==0);
        printf("coefficient index=%u input=%016llx radius=%d width=%d extended=%d sigma=%016llx present=%u shared=%u count=%u memory_delta=%lld",index++,input,c.g_r,c.g_w,c.g_w_ex,bits(c.sigma),unsigned(bool(c.g_f)),unsigned(&first->get()==&second->get()),unsigned(Fw::Count()),static_cast<long long>(g_xy_malloc_used_size)-memory);
        emit("before",before);emit("first",after_first);emit("second",after_second);emit("copy",after_copy);printf("\nvalues ");
        for(size_t i=0;i<count;++i){uint32_t v;memcpy(&v,c.g_f+i,4);printf("%08x",v);}printf("\n");
        reset();copied.reset();second.reset();first.reset();const auto released=status();
        printf("release count=%u memory_delta=%lld",unsigned(Fw::Count()),static_cast<long long>(g_xy_malloc_used_size)-memory);emit("status",released);printf("\n");
    }
    printf("gaussian-coefficients-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
