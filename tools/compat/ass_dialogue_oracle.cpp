// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ass_dialogue.h"
static uint64_t comparisons=0,accepted=0,rejected=0,records=0,callbacks=0,interrupted=0,errnoChanges=0;
static void compare(const std::wstring& input,int version,bool unicode=true,int failure=-1){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW source(input.data(),static_cast<int>(input.size()));
    CTextFile file={unicode};CSimpleTextSubtitle a,b;
    searchFailure=failure;searchAttempts=0;searchEvents.clear();errno=EDOM;
    const bool resultA=ReferenceAssDialogue::ParseDialogue(&file,a,source,version);
    const int errnoA=errno;const auto events=searchEvents;const int attempts=searchAttempts;
    searchAttempts=0;searchEvents.clear();errno=EDOM;microSide=1;
    const bool resultB=CandidateAssDialogue::ParseDialogue(&file,b,source,version);
    const int errnoB=errno;
    if(resultA!=resultB || a.entries!=b.entries || events!=searchEvents || errnoA!=errnoB){
        fprintf(stderr,"ASS dialogue mismatch version=%d unicode=%d failure=%d accepted=%d/%d records=%zu/%zu callbacks=%zu/%zu errno=%d/%d\n",version,unicode,failure,resultA,resultB,a.entries.size(),b.entries.size(),events.size(),searchEvents.size(),errnoA,errnoB);std::abort();
    }
    ++comparisons;accepted+=resultA;rejected+=!resultA;records+=a.entries.size();callbacks+=events.size();
    interrupted+=failure>=0 && attempts>failure;errnoChanges+=errnoA!=EDOM;
}
static std::wstring line(int version,const std::wstring& style=L"**dEfAuLt",const std::wstring& actor=L"Actor",const std::wstring& effect=L"effect",const std::wstring& text=L"text"){
    return std::wstring(version<=4?L"Marked=0,":L"0,")+L"0:00:01.20,0:00:04.30,"+style+L","+actor+L",1,2,3,"+(version>=6?L"4,":L"")+effect+L","+text;
}
int main(){
    microDiagnostics();
    const wchar_t* strings[]={L"",L"Default",L"**DEFAULT",L"*",L"\tDefault",L"\x2003" L"Default",L"a,b",L"shared",L"shared tail",L"{\\i1}x",L" before ",L";comment"};
    for(int version:{3,4,5,6}){
        for(const auto* style:strings)for(const auto* effect:strings)for(const auto* text:strings)
            compare(line(version,style,L"Actor",effect,text),version);
        const auto base=line(version);
        for(size_t end=0;end<=base.size();++end){
            compare(base.substr(0,end),version,false);
            std::wstring nul=base;nul.insert(end,1,wchar_t(0));compare(nul,version);
        }
        // Deterministic malformed separators and numeric tokens. Hours stay
        // bounded so the reference's timestamp arithmetic remains representable.
        const wchar_t* numbers[]={L"",L"0",L"-1",L"+7",L"0x10",L"&Hff",L" \t12junk",L"\x2003" L"12",L"0.5",L"foo",L"&h",L"0x",L"+",L"--1",L"1e9"};
        for(size_t pos=0;pos<base.size();++pos){
            std::wstring removed=base;removed.erase(pos,1);compare(removed,version);
            for(wchar_t sep:{L',',L':',L'.',L'=',L' ',L'\t'}){
                auto replaced=base;replaced[pos]=sep;compare(replaced,version);
            }
        }
        for(const auto* number:numbers){
            std::wstring text=std::wstring(version<=4?L"Marked=0,":L"0,")+number+L":00:01.20,0:00:04.30,Default,Actor,1,2,3,"+(version>=6?L"4,":L"")+L"effect,text";
            compare(text,version);
        }
        for(const auto* number:{L"2147483647",L"-2147483648",L"4294967295",L"99999999999999999999999999999999",L"&Hffffffff",L"&H80000000",L"0x100000000"}){
            std::wstring text=std::wstring(version<=4?L"Marked=":L"")+number+L",0:00:01.20,0:00:04.30,Default,Actor,"+number+L","+number+L","+number+L","+(version>=6?std::wstring(number)+L",":L"")+L"effect,text";
            compare(text,version);
        }
        for(int failure=0;failure<24;++failure)compare(base,version,true,failure);
    }
    for(unsigned unit=0;unit<=0xffff;++unit){
        std::wstring raw(1,static_cast<wchar_t>(unit));
        for(int version:{3,5,6})compare(line(version,L"*"+raw,raw,L"shared",L"shared"+raw),version,unit%2==0);
        compare(line(6,L"Default",L"Actor",raw,raw+L"tail"),6);
    }
    uint32_t random=0x6231b59d;
    const wchar_t alphabet[]=L"01,.: =\t*-+abc";
    for(unsigned n=0;n<18000;++n){std::wstring text;
        // At most five binary digits per run avoids unrepresentable hour values.
        for(unsigned i=0;i<n%73;++i){random^=random<<13;random^=random>>17;random^=random<<5;
            text+=alphabet[random%(sizeof(alphabet)/sizeof(*alphabet)-1)];if(i%5==4)text+=L',';}
        compare(text,3+int(n%4),n%2==0);
    }
    if(!accepted || !rejected || !records || !interrupted || !errnoChanges || !dialogueOwners || !dialogueSteps)return 1;
    printf("%llu exact ASS dialogue comparisons passed (%llu accepted, %llu rejected, %llu entry records, %llu callback observation values, %llu injected interruptions, %llu errno changes; %llu Rust owners / %llu steps; complete pinned dialogue branch and NextInt, versions 3-6, missing delimiters, raw UTF-16/NULs, style/effect cleanup, native numeric conversion and Add arguments; timestamp-overflow inputs and full file/style parsing excluded)\n",(unsigned long long)comparisons,(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)records,(unsigned long long)callbacks,(unsigned long long)interrupted,(unsigned long long)errnoChanges,(unsigned long long)dialogueOwners,(unsigned long long)dialogueSteps);
}
