// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_microdvd_style.h"
static uint64_t comparisons=0,callbacks=0,changed=0,errnoChanges=0,fpChanges=0;
static void compare(const std::wstring& input,int charset=1,int initialFlags=0){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CString source(input.data(),static_cast<int>(input.size()));
    searchFailure=-1;searchAttempts=0;searchEvents.clear();errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(initialFlags);
    STSStyle* a=ReferenceMicroDvdStyle::GetMicroDVDStyle(source,charset);
    const int errnoA=errno,fpA=std::fetestexcept(FE_ALL_EXCEPT);const auto output=styleSnapshot(*a);const auto events=searchEvents;delete a;
    searchAttempts=0;searchEvents.clear();errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(initialFlags);microSide=1;
    STSStyle* b=CandidateMicroDvdStyle::GetMicroDVDStyle(source,charset);
    const int errnoB=errno,fpB=std::fetestexcept(FE_ALL_EXCEPT);const auto actual=styleSnapshot(*b);delete b;
    if(output!=actual || events!=searchEvents || errnoA!=errnoB || fpA!=fpB || liveStyles){
        fprintf(stderr,"MicroDVD style mismatch charset=%d callbacks=%zu/%zu errno=%d/%d fp=%d/%d live=%d\n",charset,events.size(),searchEvents.size(),errnoA,errnoB,fpA,fpB,liveStyles);std::abort();
    }
    STSStyle defaults;++comparisons;changed+=output!=styleSnapshot(defaults);callbacks+=events.size();errnoChanges+=errnoA!=EDOM;fpChanges+=fpA!=0;
}
int main(){
    microDiagnostics();
    const wchar_t* fragments[]={L"",L"plain",L"{f:Arial}",L"{F:Other Font}",L"{f:}",L"{s:18.5}",L"{S:-2}",L"{s:bad}",L"{s:nan}",L"{s:inf}",L"{s:1e-40}",L"{c:$ff0011}",L"{C:$ffffffff}",L"{c:$bad!}",L"{c:$}",L"{h:128}",L"{H:-1}",L"{h:bad}",L"{y:BIUS}",L"{y:}",L"{p:0}",L"{P:1}",L"{p:-1}",L"{unknown}",L"{",L"}",L"{{f:ignored}",L"{f:unterminated",L"{p:0"};
    for(const auto* a:fragments)for(const auto* b:fragments)for(int charset:{-1,0,1,128,255})compare(std::wstring(a)+b,charset);
    for(unsigned unit=0;unit<=0xffff;++unit){
        std::wstring s=L"{f:";s.push_back(static_cast<wchar_t>(unit));s+=L"}{y:i}";compare(s);
        s=L"{";s.push_back(static_cast<wchar_t>(unit));s+=L"x}{f:tail}";compare(s);
    }
    for(int charset=0;charset<256;++charset)for(const auto* fragment:fragments)compare(fragment,charset);
    uint32_t random=0x315684d7;
    for(unsigned n=0;n<16000;++n){std::wstring s;
        for(unsigned i=0;i<n%15;++i){random^=random<<13;random^=random>>17;random^=random<<5;s+=fragments[random%(sizeof(fragments)/sizeof(*fragments))];}
        compare(s,int(n%256));
    }
    for(int flags:{FE_INVALID,FE_DIVBYZERO})compare(L"{c:$ff00}{h:128}{y:i}{p:0}{s:18}",1,flags);
    if(!changed || !microStyleOwners || !microStyleSteps || !microStyleScans || !fpChanges)return 1;
    printf("%llu exact GetMicroDVDStyle comparisons passed (%llu changed styles, %llu callback observation values, %llu errno changes, %llu nonzero floating-status observations including seeded flags; %llu Rust owners / %llu traversal steps; all style fields and float bits, raw UTF-16/NULs, nested/unclosed tags, persistent flags, ignored input charset, native prefix/case/scan order; position fixtures require successful scans, overflowing scanf conversions and allocation failures excluded)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)callbacks,(unsigned long long)errnoChanges,(unsigned long long)fpChanges,(unsigned long long)microStyleOwners,(unsigned long long)microStyleSteps);
}
