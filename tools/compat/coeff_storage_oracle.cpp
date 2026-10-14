// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_coeff_storage.h"
#include <boost/flyweight.hpp>
#include <boost/flyweight/key_value.hpp>
#include <boost/flyweight/no_locking.hpp>
static uint64_t cacheHistories=0,histories=0,fields=0,bytes=0,copies=0,failures=0,retained=0,excludedCopies=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
static void effects(std::vector<uint64_t>& out){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
const unsigned long last=GetLastError();
#else
const unsigned long last=0;
#endif
out.insert(out.end(),{static_cast<uint64_t>(error),static_cast<uint64_t>(flags),last});}
template<class T>static void initialized(const T& value){if(value.g_f&&value.g_w>0)coeffRegion(value.g_f).initialized=true;}
template<class T>static void snapshot(const T& v,std::vector<uint64_t>& out,bool count){
    uint64_t sigma=0;static_assert(sizeof(sigma)==sizeof(v.sigma),"double size");std::memcpy(&sigma,&v.sigma,sizeof(sigma));
    out.insert(out.end(),{static_cast<uint64_t>(v.g_r),static_cast<uint64_t>(v.g_w),static_cast<uint64_t>(v.g_w_ex),sigma,v.g_f?1U:0U,g_xy_malloc_used_size});
    if(v.g_f){const auto& r=coeffRegion(v.g_f);coeffRequire(r.initialized&&reinterpret_cast<uintptr_t>(v.g_f)%16==0);coeffGuards(r);out.push_back(r.count);out.insert(out.end(),r.data,r.data+r.count);bytes+=count?r.count:0;}
}
struct Outcome {std::vector<uint64_t> fields,events;bool operator==(const Outcome& o)const{return fields==o.fields&&events==o.events;}};
template<class T>static Outcome execute(double first,double second,bool decline,int failStage,bool count){
    coeffRequire(coeffRegions.empty()&&g_xy_malloc_used_size==0);coeffEvents.clear();coeffDecline=decline;coeffFail=failStage==0;Outcome result;auto& out=result.fields;
    seed();T* value=new T(first);effects(out);initialized(*value);snapshot(*value,out,count);
    coeffFail=failStage==1;seed();int status=value->init(second);effects(out);out.push_back(static_cast<uint64_t>(status));failures+=count&&status==-1;initialized(*value);snapshot(*value,out,count);retained+=count&&value->g_w_ex<=0&&value->g_f;
    coeffFail=false;
    // A native copy needs a complete source if its metadata requests bytes.
    // Failed initialization with a positive count is recorded, never copied.
    if(value->g_w_ex<=0||value->g_f){
        seed();T* copy=new T(*value);effects(out);snapshot(*copy,out,count);snapshot(*value,out,count);copies+=count;
        if(copy->g_f){coeffRequire(copy->g_f!=value->g_f);uint32_t marker=0x3f123456;std::memcpy(copy->g_f,&marker,sizeof(marker));snapshot(*copy,out,count);snapshot(*value,out,count);}
        seed();delete value;effects(out);snapshot(*copy,out,count);
        seed();delete copy;effects(out);
    }else{excludedCopies+=count;seed();delete value;effects(out);}
    coeffRequire(coeffRegions.empty()&&g_xy_malloc_used_size==0);out.push_back(g_xy_malloc_used_size);result.events=coeffEvents;return result;
}
template<class T>struct CoeffKey {const double& operator()(const T& value)const{return value.sigma;}};
template<class T>static Outcome cacheHistory(double first,double second,bool decline,bool count){
    using F=boost::flyweight<boost::flyweights::key_value<double,T,CoeffKey<T>>,boost::flyweights::no_locking>;
    coeffRequire(coeffRegions.empty()&&!g_xy_malloc_used_size);coeffEvents.clear();coeffDecline=decline;coeffFail=false;Outcome result;auto& out=result.fields;
    {
        seed();F a(first);effects(out);initialized(a.get());snapshot(a.get(),out,count);
        seed();F b(second);effects(out);initialized(b.get());snapshot(b.get(),out,count);
        seed();F alias(a);effects(out);coeffRequire(&alias.get()==&a.get());snapshot(alias.get(),out,count);
        out.push_back(&a.get()==&b.get());
        seed();b=a;effects(out);coeffRequire(&a.get()==&b.get());snapshot(a.get(),out,count);
        seed();
    }
    effects(out);coeffRequire(coeffRegions.empty()&&!g_xy_malloc_used_size);result.events=coeffEvents;return result;
}
int main(){
    const double sigmas[]={-10,-3,-1,-0.5,-0.25,-0.0,0.0,1e-300,1e-154,1e-6,0.1,0.25,0.5,1,1.3333333333333333,2,3,7,14,31,63};
    for(double first:sigmas)for(double second:sigmas)for(bool decline:{false,true})for(int failure:{-1,0,1}){
        auto a=execute<ReferenceCoefficients::GaussianCoefficients>(first,second,decline,failure,true);
        auto b=execute<CandidateCoefficients::GaussianCoefficients>(first,second,decline,failure,false);
        if(!(a==b)){fprintf(stderr,"Coefficient mismatch first=%.17g second=%.17g decline=%d failure=%d\n",first,second,decline,failure);for(size_t i=0;i<std::min(a.fields.size(),b.fields.size());++i)if(a.fields[i]!=b.fields[i])fprintf(stderr,"field[%llu] %llu/%llu\n",(unsigned long long)i,(unsigned long long)a.fields[i],(unsigned long long)b.fields[i]);std::abort();}
        ++histories;fields+=a.fields.size()+a.events.size();
    }
    for(double first:sigmas)for(double second:sigmas)for(bool decline:{false,true}){
        const auto a=cacheHistory<ReferenceCoefficients::GaussianCoefficients>(first,second,decline,true);
        const auto b=cacheHistory<CandidateCoefficients::GaussianCoefficients>(first,second,decline,false);
        if(!(a==b)){fprintf(stderr,"Coefficient flyweight mismatch %.17g/%.17g decline=%d\n",first,second,decline);std::abort();}
        ++cacheHistories;fields+=a.fields.size()+a.events.size();
    }
    coeffRequire(histories&&bytes&&copies&&failures&&retained&&excludedCopies&&coeffOwners&&coeffOwners==coeffLoans&&coeffLoans==coeffReleases&&coeffDeclines);
    printf("%llu exact Gaussian coefficient histories passed (%llu state/byte/allocation fields / %llu initialized bytes / %llu independent deep copies / %llu real vendored flyweight cache histories / %llu modeled init failures / %llu retained-table negative-width states / %llu invalid-source copy exclusions NOT passes / %llu Rust owners, loans and releases / %llu modeled Rust declines; pinned complete constructor, copy, init and destructor; exact float bits, signed-zero and tiny-sigma behavior, same-key early return, negative widths, mutation independence, logical cache accounting and errno/FP/Windows error state under strict-FP compilation; physical prefix remains unwritten, allocation failures modeled, copy allocation failure/nonfinite or overflowing widths/actual OOM excluded)\n",(unsigned long long)histories,(unsigned long long)fields,(unsigned long long)bytes,(unsigned long long)copies,(unsigned long long)cacheHistories,(unsigned long long)failures,(unsigned long long)retained,(unsigned long long)excludedCopies,(unsigned long long)coeffOwners,(unsigned long long)coeffDeclines);
}
