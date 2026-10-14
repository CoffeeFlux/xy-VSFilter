// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_microdvd.h"
static uint64_t comparisons=0,changed=0,units=0,callbacks=0,interruptions=0;
static std::vector<uint16_t> contents(const CStringW& text){std::vector<uint16_t> out;for(int i=0;i<text.GetLength();++i)out.push_back(static_cast<uint16_t>(text[i]));return out;}
static void compare(const std::wstring& input,bool unicode,int charset,int failure=-1){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW source(input.data(),static_cast<int>(input.size()));auto before=contents(source);
    std::vector<uint16_t> a,b;bool ia=false,ib=false;
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();
    try{a=contents(ReferenceMicroDVD::RemoveSSATags(source,unicode,charset));}catch(const SearchInterrupted&){ia=true;}
    auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;
    try{b=contents(CandidateMicroDVD::RemoveSSATags(source,unicode,charset));}catch(const SearchInterrupted&){ib=true;}
    if(a!=b || ia!=ib || events!=searchEvents || before!=contents(source)){fprintf(stderr,"RemoveSSATags mismatch case=%llu length=%zu unicode=%d charset=%d failure=%d\n",(unsigned long long)comparisons,input.size(),unicode,charset,failure);std::abort();}
    ++comparisons;changed+=!ia && a!=before;units+=a.size();callbacks+=events.size();interruptions+=ia;
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"",L"{",L"}",L"x",L"{\\i1}",L"{\\i}",L"{\\I1}",L"{\\b1}",L"\\N",L"\\n",L"\\h",L"\\",L"N",L"{\\fe128}",L"{\\fe}",L"{\\i1",L"{\\fe128"};
    const unsigned base=sizeof(pieces)/sizeof(*pieces);unsigned combinations=1;
    for(unsigned len=0;len<=4;++len){if(len)combinations*=base;for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;for(unsigned i=0;i<len;++i){input+=pieces[code%base];code/=base;}compare(input,value%2==0,value%3==0?128:0);}}
    for(unsigned unit=1;unit<=0xffff;++unit){std::wstring input=L"{\\i1}";input.push_back(wchar_t(unit));input+=L"{x}\\N{\\i}";compare(input,false,128);compare(input,true,0);}
    uint32_t random=0x68493271;
    for(unsigned value=0;value<18000;++value){std::wstring input;for(unsigned i=0;i<value%25;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=pieces[random%base];}compare(input,value%2==0,int(value%256));}
    for(unsigned value=0;value<256;++value){std::wstring input=L"before";input.push_back(0);input+=L"AFTER";compare(input,value%2==0,int(value));}
    for(int failure=0;failure<240;++failure)compare(L"{\\i1}one{\\fe128}two{{nested}tail}\\N{\\i}end{open",false,128,failure);
    const uint16_t nul[]={92,78,0};if(xy_vsfilter_remove_tags_create(nul,3)!=NULL){fprintf(stderr,"RemoveSSATags NUL control failed\n");return 1;}
    if(!changed || !interruptions || !removeHandled || !removeFallbacks || !removeSteps){fprintf(stderr,"RemoveSSATags route controls failed\n");return 1;}
    printf("%llu exact whole SSA-tag-removal conversions passed (%llu changed strings, %llu output UTF-16 units, %llu callback observation values, %llu injected interruptions, %llu Rust owners / %llu steps / %llu native NUL fallbacks; italic substitutions before removal, first closing brace, unterminated tails, newline/space ordering, every nonzero UTF-16 value and both charset-search modes; replacement-bearing NUL outcomes excluded)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)units,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)removeHandled,(unsigned long long)removeSteps,(unsigned long long)removeFallbacks);
}
