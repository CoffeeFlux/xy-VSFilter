// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ellipse_arc.h"
static uint64_t comparisons=0,fields=0,changed=0,repeated=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
struct Effects {int error,flags;unsigned long last;Effects():error(errno),flags(std::fetestexcept(FE_ALL_EXCEPT)),last(0){
#ifdef _WIN32
last=GetLastError();
#endif
}bool operator==(const Effects& o)const{return error==o.error&&flags==o.flags&&last==o.last;}};
static void compare(int rx,int ry,bool again=false){
    size_t n=size_t(ry)*2+1;std::vector<int> a(n+19),b;for(size_t i=0;i<a.size();++i)a[i]=static_cast<int32_t>(0xa59e31c7u+uint32_t(i)*173u);b=a;auto before=a;
    int rounds=again?3:1;
    for(int i=0;i<rounds;++i){
        seed();int ra=ReferenceEllipse::gen_left_arc(a.data()+8,rx+i,ry);Effects ea;
        seed();int rb=CandidateEllipse::gen_left_arc(b.data()+8,rx+i,ry);Effects eb;
        if(ra!=rb||a!=b||!(ea==eb)){fprintf(stderr,"Ellipse mismatch rx=%d ry=%d round=%d\n",rx,ry,i);std::abort();}
        for(size_t p=0;p<a.size();++p)if((p<8||p>=8+n)&&a[p]!=before[p])std::abort();
        fields+=a.size();changed+=a!=before;++comparisons;
    }
    repeated+=again;
}
int main(){
    for(int rx=0;rx<=256;++rx)for(int ry=0;ry<=256;++ry)compare(rx,ry);
    for(int radius:{257,511,512,513,1023,2048,4096,8192})for(int other:{0,1,2,3,7,16,31,64,127}){compare(radius,other,true);compare(other,radius,true);}
    uint32_t random=0x728ea41d;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(int i=0;i<12000;++i)compare(next()%1025,next()%1025,true);
    // Null output returns before consulting radii, including signed extremes.
    for(int rx:{INT32_MIN,-1,0,1,INT32_MAX})for(int ry:{INT32_MIN,-1,0,1,INT32_MAX}){
        seed();int a=ReferenceEllipse::gen_left_arc(NULL,rx,ry);Effects ea;seed();int b=CandidateEllipse::gen_left_arc(NULL,rx,ry);Effects eb;
        if(a!=b||!(ea==eb))std::abort();++comparisons;
    }
    // Direct candidate decline, separate from native comparisons: the native
    // intermediate overflow is never executed or counted as a passing case.
    int decline[]={71,72,73,74,75};
    if(XyRust::GenerateEllipseArc(decline,INT32_MAX,2))std::abort();
    for(int i=0;i<5;++i)if(decline[i]!=71+i)std::abort();
    if(!ellipseHandled||ellipseFallback||!changed||!repeated)std::abort();
    printf("%llu exact integer ellipse-arc comparisons passed (%llu guarded buffer fields / %llu changed buffers / %llu repeated generation histories / %llu Rust generators / %llu native fallbacks; exhaustive nonnegative radii 0..256, degenerate axes, aspect ratios through 8192, null outputs, native update/fill order including base-relative read, untouched padding, errno/FP/Windows error state; representable arithmetic and successful allocation, native float intersections retained; separate transactional overflow decline check)\n",(unsigned long long)comparisons,(unsigned long long)fields,(unsigned long long)changed,(unsigned long long)repeated,(unsigned long long)ellipseHandled,(unsigned long long)ellipseFallback);
}
