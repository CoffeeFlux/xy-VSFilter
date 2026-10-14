// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_sami.h"
static uint64_t comparisons=0,changed=0,units=0,callbacks=0,interruptions=0,errnoChanges=0;
static std::vector<uint16_t> contents(const CStringW& text){std::vector<uint16_t> out;for(int i=0;i<text.GetLength();++i)out.push_back(static_cast<uint16_t>(text[i]));return out;}
static void compare(const std::wstring& input,int charset,int failure=-1){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW source(input.data(),static_cast<int>(input.size()));auto before=contents(source);
    std::vector<uint16_t> a,b;bool ia=false,ib=false;
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();microHook=NULL;errno=EDOM;
    try{a=contents(ReferenceSami::SMI2SSA(source,charset));}catch(const SearchInterrupted&){ia=true;}
    int ea=errno;auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;errno=EDOM;
    try{b=contents(CandidateSami::SMI2SSA(source,charset));}catch(const SearchInterrupted&){ib=true;}
    int eb=errno;
    if(a!=b || ia!=ib || events!=searchEvents || ea!=eb || before!=contents(source)){
        fprintf(stderr,"SAMI mismatch case=%llu length=%zu charset=%d failure=%d output=%zu/%zu errno=%d/%d callbacks=%zu/%zu\n",(unsigned long long)comparisons,input.size(),charset,failure,a.size(),b.size(),ea,eb,events.size(),searchEvents.size());std::abort();
    }
    ++comparisons;changed+=!ia && a!=before;units+=a.size();callbacks+=events.size();interruptions+=ia;errnoChanges+=ea!=EDOM;
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"",L"A",L"<",L">",L"<x>",L"<i>",L"</I>",L"<BR>",L"&NBSP;",L"&quot;",L"<font color=red>",L"<font color=\"#000000\">",L"<font color=black>",L"</FONT>",L"<font color=red",L"<font color=red color=blue>",L"<font color=>",L"<font size=24>",L"<font ",L"<font color =blue>",L"<font color=0x123456>",L"<font color='red'>"};
    const unsigned base=sizeof(pieces)/sizeof(*pieces);unsigned combinations=1;
    for(unsigned len=0;len<=3;++len){if(len)combinations*=base;for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;for(unsigned i=0;i<len;++i){input+=pieces[code%base];code/=base;}compare(input,int(value%256));}}
    for(unsigned unit=1;unit<=0xffff;++unit){std::wstring input=L"A<font color=\"";input.push_back(wchar_t(unit));input+=L"\">B</font>C";compare(input,128);}
    for(const auto& entry:ReferenceSami::htmlcolors){std::wstring input=L"<FONT COLOR=";input+=entry.name;input+=L">known</FONT>";compare(input,0);}
    const wchar_t* values[]={L"0",L"000000",L"-1",L"7fffffff",L"80000000",L"ffffffff",L"100000000",L"7fffffffffffffff",L"8000000000000000",L"10000000000000000",L"+",L"-",L"red",L"unknown",L"ffffffjunk"};
    for(const auto* value:values){std::wstring input=L"<font color=";input+=value;input+=L" color=blue>numeric</font>";compare(input,-1);}
    uint32_t random=0x42763189;
    for(unsigned value=0;value<20000;++value){std::wstring input;for(unsigned i=0;i<value%21;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=pieces[random%base];}compare(input,int(value%256));}
    for(int failure=0;failure<100;++failure)compare(L"<I>one</i><font color=red color=#123456 color=0>two</font><font color=blue",128,failure);
    for(unsigned i=0;i<256;++i){std::wstring input=L"before";input.push_back(0);input+=L"AFTER";compare(input,int(i));}
    const uint16_t nul[]={60,0,62};if(xy_vsfilter_sami_create(nul,3,nul,3)!=NULL){fprintf(stderr,"SAMI NUL decline control failed\n");return 1;}
    if(!changed || !samiHandled || !samiFallbacks || !samiInsertions || !interruptions){fprintf(stderr,"SAMI route controls failed\n");return 1;}
    printf("%llu exact whole SAMI conversions passed (%llu changed strings, %llu output UTF-16 units, %llu callback observation values, %llu injected interruptions, %llu observed errno changes, %llu Rust owners / %llu steps / %llu color insertions / %llu native NUL fallbacks; all named reference colors, numeric/default color rules, repeated attributes, malformed/unterminated tags, every nonzero UTF-16 value and ordered entity/style preprocessing; replacement-bearing NUL outcomes excluded)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)units,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)errnoChanges,(unsigned long long)samiHandled,(unsigned long long)samiSteps,(unsigned long long)samiInsertions,(unsigned long long)samiFallbacks);
}
