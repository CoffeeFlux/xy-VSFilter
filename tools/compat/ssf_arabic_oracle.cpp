// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_arabic.h"
static uint64_t checks=0,changed=0,zeroes=0,retained=0,repeated=0;
struct Result {WCHAR value;bool handled;int error,flags;unsigned long last;
    bool operator==(const Result& o)const{return value==o.value&&handled==o.handled&&error==o.error&&flags==o.flags&&last==o.last;}};
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
template<class Arabic>static Result invoke(unsigned unit,int mode,unsigned previous,unsigned next){
    seed();WCHAR value=static_cast<WCHAR>(unit);bool handled;
    if(mode==-2)handled=Arabic::IsArabic(value);
    else if(mode==-1)handled=Arabic::Replace(value,static_cast<WCHAR>(previous),static_cast<WCHAR>(next));
    else handled=Arabic::Replace(value,static_cast<typename Arabic::pres_form_t>(mode));
    const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
    const unsigned long last=GetLastError();
#else
    const unsigned long last=0;
#endif
    return Result{value,handled,error,flags,last};
}
static unsigned compare(unsigned unit,int mode,unsigned previous=0,unsigned next=0){
    const Result a=invoke<ReferenceArabic::Arabic>(unit,mode,previous,next),b=invoke<CandidateArabic::Arabic>(unit,mode,previous,next);
    if(!(a==b)){fprintf(stderr,"SSF Arabic mismatch unit=%u mode=%d previous=%u next=%u\n",unit,mode,previous,next);std::abort();}
    ++checks;changed+=a.value!=unit;zeroes+=a.handled&&a.value==0;retained+=!a.handled;
    return a.value;
}
int main(){
    for(unsigned unit=0;unit<=0xffff;++unit){
        compare(unit,-2);
        for(int form=0;form<4;++form)compare(unit,form);
        for(unsigned p:{0U,0x600U,0x6ffU,0xffffU})for(unsigned n:{0U,0x600U,0x6ffU,0xffffU})compare(unit,-1,p,n);
        compare(unit,-1,unit,unit);
    }
    for(unsigned neighbor=0;neighbor<=0xffff;++neighbor)
        for(unsigned unit:{0U,0x604U,0x621U,0x626U,0x644U,0x6ffU,0xd800U})
            for(unsigned other:{0U,0x600U,0x6ffU,0xffffU}){
                compare(unit,-1,neighbor,other);compare(unit,-1,other,neighbor);
            }
    for(unsigned unit=0x600;unit<=0x6ff;++unit)for(unsigned p:{0U,0x600U,0x6ffU,0xffffU})for(unsigned n:{0U,0x600U,0x6ffU,0xffffU}){
        unsigned c=unit;for(unsigned step=0;step<8;++step){c=compare(c,-1,p,n);++repeated;}
    }
    if(!(changed&&zeroes&&retained&&repeated&&arabicClassifications&&arabicForms&&arabicNeighbors))std::abort();
    printf("%llu exact SSF Arabic classification/replacement comparisons passed (%llu changed units / %llu handled zero results / %llu unchanged out-of-range results / %llu repeated replacements / %llu Rust classifications / %llu Rust form plans / %llu Rust neighbor plans; all UTF-16 units and four presentation forms, every neighbor code unit in both positions, range endpoints, zero table entries, raw surrogates and original neighbor snapshots, errno/FP/Windows error state; complete pinned table and methods, font selection, shaping outside these methods and SSF renderer integration excluded)\n",(unsigned long long)checks,(unsigned long long)changed,(unsigned long long)zeroes,(unsigned long long)retained,(unsigned long long)repeated,(unsigned long long)arabicClassifications,(unsigned long long)arabicForms,(unsigned long long)arabicNeighbors);
}
