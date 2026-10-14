// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ass_style.h"
static uint64_t comparisons=0,accepted=0,rejected=0,callbacks=0,interruptions=0,errnoChanges=0,fpChanges=0;
static std::vector<uint16_t> units(const CStringW& str){std::vector<uint16_t> v;for(int i=0;i<str.GetLength();++i)v.push_back(static_cast<uint16_t>(str[i]));return v;}
static void compare(const std::wstring& input,int version,int failure=-1,int initialFlags=0){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW a(input.data(),static_cast<int>(input.size())),b=a;CSimpleTextSubtitle first,second;
    searchFailure=failure;searchAttempts=0;searchEvents.clear();errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(initialFlags);
    const bool resultA=ReferenceAssStyle::ParseStyle(first,a,version);
    const int errnoA=errno,fpA=std::fetestexcept(FE_ALL_EXCEPT);const auto events=searchEvents;const int attempts=searchAttempts;
    if(liveStyles)std::abort();
    searchAttempts=0;searchEvents.clear();errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(initialFlags);microSide=1;
    const bool resultB=CandidateAssStyle::ParseStyle(second,b,version);
    const int errnoB=errno,fpB=std::fetestexcept(FE_ALL_EXCEPT);
    if(liveStyles || resultA!=resultB || first.entries!=second.entries || units(a)!=units(b) || events!=searchEvents || errnoA!=errnoB || fpA!=fpB){
        fprintf(stderr,"ASS style mismatch version=%d failure=%d accepted=%d/%d entries=%zu/%zu callbacks=%zu/%zu errno=%d/%d fp=%d/%d live=%d\n",version,failure,resultA,resultB,first.entries.size(),second.entries.size(),events.size(),searchEvents.size(),errnoA,errnoB,fpA,fpB,liveStyles);std::abort();
    }
    ++comparisons;accepted+=resultA;rejected+=!resultA;callbacks+=events.size();interruptions+=failure>=0 && attempts>failure;errnoChanges+=errnoA!=EDOM;fpChanges+=fpA!=0;
}
static std::vector<std::wstring> fields(int version){
    std::vector<std::wstring> f={L"**Default",L"Arial",L"18",L"16777215",L"255",L"0",L"0",L"-1",L"0"};
    if(version>=5)for(auto s:{L"0",L"0",L"100",L"100",L"0",L"0"})f.push_back(s);
    if(version>=4)f.push_back(L"1");
    for(auto s:{L"2",L"3",L"2",L"20",L"20",L"20"})f.push_back(s);
    if(version>=6)f.push_back(L"20");
    if(version<=4)f.push_back(L"0");
    f.push_back(L"1");if(version>=6)f.push_back(L"2");return f;
}
static std::wstring join(const std::vector<std::wstring>& fields){
    std::wstring s;for(size_t i=0;i<fields.size();++i){if(i)s+=L',';s+=fields[i];}return s;
}
int main(){
    microDiagnostics();
    // GetStr's private TrimLeft operation was already replaced by StringField;
    // compare actual field effects, plus retained MakeLower/scanf/Add callbacks.
    microObserveFieldTrims=false;
    for(int version:{3,4,5,6}){
        const auto base=fields(version);const auto text=join(base);
        compare(text,version,-1,FE_DIVBYZERO);compare(text,version,-1,FE_INVALID);
        for(size_t end=0;end<=text.size();++end){compare(text.substr(0,end),version);auto nul=text;nul.insert(end,1,wchar_t(0));compare(nul,version);}
        for(size_t field=0;field<base.size();++field){
            for(auto value:{L"",L" ",L"\t1",L"\x2003" L"1",L"-1",L"0",L"1",L"255",L"256",L"-255",L"0x10",L"&Hff",L"junk",L"1junk",L"1,2"}){
                auto f=base;f[field]=value;compare(join(f),version);
            }
        }
        // Float inputs are representable float values, including explicit
        // nonfinite spellings; no overflowing scanf numeric conversions.
        for(auto value:{L"-0",L"0.125",L"-2.5",L"1e-40",L"3.4028234e38",L"nan",L"-nan",L"inf",L"-inf",L"infinity",L"0x1p2"}){
            auto f=base;f[2]=value;compare(join(f),version);
            if(version>=5)for(size_t i:{size_t(11),size_t(12),size_t(13),size_t(14)}){f=base;f[i]=value;compare(join(f),version);}
        }
        for(unsigned color=0;color<256;++color){
            auto f=base;wchar_t number[32];std::swprintf(number,32,L"&H%08x",(color<<24)|0x123456);f[3]=number;f[6]=number;
            compare(join(f),version);
        }
        const size_t outline=version>=5?16:(version>=4?10:9),alignment=outline+2;
        for(int value=-256;value<512;++value){
            auto f=base;f[alignment]=std::to_wstring(value);if(version<=4)f[f.size()-2]=std::to_wstring(value);
            compare(join(f),version);
        }
        for(int value:{-2147483647-1,-1,0,1,2,128,255,2147483647}){
            auto f=base;f[f.size()-(version>=6?2:1)]=std::to_wstring(value);compare(join(f),version);
        }
        for(int failure=0;failure<60;++failure)compare(text,version,failure);
    }
    for(unsigned unit=0;unit<=0xffff;++unit)for(int version:{3,6}){
        auto f=fields(version);f[0]=L"*";f[0].push_back(static_cast<wchar_t>(unit));f[1]=f[0];compare(join(f),version);
    }
    uint32_t random=0x191034ce;
    for(unsigned n=0;n<12000;++n){int version=3+int(n%4);auto f=fields(version);
        random^=random<<13;random^=random>>17;random^=random<<5;
        const size_t index=random%f.size();f[index]=std::to_wstring(int(random%2001)-1000);
        std::wstring text=join(f);if(n%3==0)text.resize(random%(text.size()+1));compare(text,version);
    }
    if(!accepted || !rejected || !interruptions || !fpChanges || !styleOwners || !styleAccepts || !styleNormalizations){
        fprintf(stderr,"Style route controls failed: accepted=%llu rejected=%llu interruptions=%llu fp=%llu owners=%llu accepts=%llu normalizations=%llu\n",(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)interruptions,(unsigned long long)fpChanges,(unsigned long long)styleOwners,(unsigned long long)styleAccepts,(unsigned long long)styleNormalizations);return 1;
    }
    printf("%llu exact ASS style comparisons passed (%llu accepted, %llu rejected, %llu callback observation values, %llu injected interruptions, %llu errno changes, %llu nonzero floating-status observations including seeded flags; %llu Rust owners / %llu field steps / %llu accepted remainders / %llu integer normalizations; complete pinned style branch, native defaults, all stored fields and float bits, versions 3-6, raw UTF-16/NULs, field failures, alpha/color/alignment mappings and native floating conversion/clamps; GetStr private trim call identity and overflowing scanf conversions excluded)\n",(unsigned long long)comparisons,(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)errnoChanges,(unsigned long long)fpChanges,(unsigned long long)styleOwners,(unsigned long long)styleSteps,(unsigned long long)styleAccepts,(unsigned long long)styleNormalizations);
}
