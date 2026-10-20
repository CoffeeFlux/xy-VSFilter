// SPDX-License-Identifier: GPL-2.0-or-later
// Pinned native weak table and linked coefficient init versus the Rust owner.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <process.h>
#include <cfenv>
#include <array>
#include "xy_bitmap.h"
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
class GaussianCoefficients {
public:
    int g_r,g_w,g_w_ex;float* g_f;double sigma;
    explicit GaussianCoefficients(double value):g_r(0),g_w(0),g_w_ex(0),g_f(nullptr),sigma(0){init(value);}
    ~GaussianCoefficients(){xy_free(g_f);}
private:int init(double);
};
#endif
static uint64_t bits(double value){uint64_t out;memcpy(&out,&value,8);return out;}

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include "RustDocumentOwner.h"
extern "C" {
uint32_t __cdecl xy_vsfilter_gaussian_cache_acquire_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_gaussian_cache_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_gaussian_cache_release_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_gaussian_cache_stats_v1(uint8_t*);
}
class Lease {
    uint64_t id_;std::vector<uint8_t> packet_;
public:
    uint64_t identity;
    explicit Lease(uint64_t key){using namespace XyRust::DocumentDetail;std::array<uint8_t,24> out;
        Check(Invoke([&](){return xy_vsfilter_gaussian_cache_acquire_v1(key,out.data());}));id_=Load(out,0,8);identity=Load(out,8,8);
        packet_.resize(Size(Load(out,16,8)));Check(Invoke([&](){return xy_vsfilter_gaussian_cache_read_v1(id_,packet_.data(),packet_.size());}));}
    ~Lease(){XyRust::DocumentDetail::Release(XyRust::DocumentDetail::Invoke([&](){return xy_vsfilter_gaussian_cache_release_v1(id_);}));}
    const std::vector<uint8_t>& packet()const{return packet_;}
    static std::array<uint64_t,2> stats(){using namespace XyRust::DocumentDetail;std::array<uint8_t,16> out;Check(Invoke([&](){return xy_vsfilter_gaussian_cache_stats_v1(out.data());}));return {Load(out,0,8),Load(out,8,8)};}
};
#else
class Lease {
    using Fw=XyInternedByKey<double,GaussianCoefficients>;std::unique_ptr<Fw> value_;std::vector<uint8_t> packet_;
public:
    uint64_t identity;
    explicit Lease(uint64_t key){double sigma;memcpy(&sigma,&key,8);value_=std::make_unique<Fw>(sigma);identity=reinterpret_cast<uintptr_t>(&value_->get());
        const auto& c=value_->get();const size_t count=c.g_f?size_t(c.g_w_ex):0;check(count<4096);packet_.resize(24+count*4);
        const int fields[]={c.g_r,c.g_w,c.g_w_ex,c.g_f?1:0};memcpy(packet_.data(),fields,16);memcpy(packet_.data()+16,&c.sigma,8);if(count)memcpy(packet_.data()+24,c.g_f,count*4);}
    const std::vector<uint8_t>& packet()const{return packet_;}
    static std::array<uint64_t,2> stats(){return {Fw::Count(),g_xy_malloc_used_size};}
};
#endif
static uint64_t read(const std::vector<uint8_t>& bytes,size_t at,size_t n){uint64_t v=0;memcpy(&v,bytes.data()+at,n);return v;}
static unsigned cache_index=0;
static void observe(uint64_t key,unsigned sticky,bool retain,std::vector<std::unique_ptr<Lease>>& held){
    reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);const auto before=status();
    auto first=std::make_unique<Lease>(key);const auto one=status();auto second=std::make_unique<Lease>(key);const auto two=status();
    const auto stats=Lease::stats();const auto& p=first->packet();
    printf("cache index=%u input=%016llx retain=%u radius=%d width=%d extended=%d sigma=%016llx present=%u shared=%u count=%llu bytes=%llu",cache_index++,key,unsigned(retain),int32_t(read(p,0,4)),int32_t(read(p,4,4)),int32_t(read(p,8,4)),read(p,16,8),unsigned(read(p,12,4)),unsigned(first->identity==second->identity),stats[0],stats[1]);
    emit("before",before);emit("first",one);emit("second",two);printf("\nvalues ");for(size_t i=24;i<p.size();i+=4)printf("%08x",uint32_t(read(p,i,4)));printf("\n");
    reset();if(retain)held.push_back(std::move(first));second.reset();first.reset();const auto released=status();const auto end=Lease::stats();
    printf("release count=%llu bytes=%llu",end[0],end[1]);emit("status",released);printf("\n");
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    std::vector<uint64_t> inputs={0,0x8000000000000000ULL,1,0x000fffffffffffffULL,0xbfb999999999999aULL,0xbff0000000000000ULL,0xc010000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000001ULL,0xfff8000000000001ULL,0x7ff0000000000001ULL,0xfff0000000000001ULL,0x7fefffffffffffffULL};
    const double values[]={0.000001,0.1,0.333333,0.33333333333333331,0.5,1,1.5,2,4,8,16,32,128};
    for(double value:values){const auto v=bits(value);inputs.push_back(v-1);inputs.push_back(v);inputs.push_back(v+1);}
    printf("gaussian-cache pointer_bits=%u inputs=%u\n",unsigned(sizeof(void*)*8),unsigned(inputs.size()));
    std::vector<std::unique_ptr<Lease>> held;
    for(unsigned repeat=0;repeat<2;++repeat)for(unsigned sticky=0;sticky<2;++sticky)for(auto key:inputs)observe(key,sticky,false,held);
    unsigned drops=0;
    for(unsigned sticky=0;sticky<2;++sticky){for(unsigned i=0;i<96;++i)observe(0x3fd0000000000000ULL+(uint64_t(i+1)<<40),sticky,true,held);
        while(!held.empty()){reset();if(sticky)std::feraiseexcept(FE_DIVBYZERO);held.pop_back();const auto after=status();const auto counts=Lease::stats();printf("drop index=%u count=%llu bytes=%llu",drops++,counts[0],counts[1]);emit("status",after);printf("\n");}}
    printf("gaussian-cache-complete cases=%u drops=%u\n",cache_index,drops);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
