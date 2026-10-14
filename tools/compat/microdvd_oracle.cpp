// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_microdvd.h"
static uint64_t comparisons=0,changed=0,units=0,callbacks=0,interruptions=0;
static std::vector<uint16_t> contents(const CStringW& text){std::vector<uint16_t> out;for(int i=0;i<text.GetLength();++i)out.push_back(static_cast<uint16_t>(text[i]));return out;}
static void compare(const std::wstring& input,bool unicode,int charset,int failure=-1){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW source(input.data(),static_cast<int>(input.size()));const auto before=contents(source);
    std::vector<uint16_t> a,b;bool ia=false,ib=false;
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();
    try{a=contents(ReferenceMicroDVD::MicroDVD2SSA(source,unicode,charset));}catch(const SearchInterrupted&){ia=true;}
    const auto events=searchEvents;searchAttempts=0;searchEvents.clear();
    microSide=1;
    try{b=contents(CandidateMicroDVD::MicroDVD2SSA(source,unicode,charset));}catch(const SearchInterrupted&){ib=true;}
    if(a!=b || ia!=ib || events!=searchEvents || contents(source)!=before){fprintf(stderr,"MicroDVD mismatch case=%llu length=%zu unicode=%d charset=%d failure=%d output=%zu/%zu callbacks=%zu/%zu\n",(unsigned long long)comparisons,input.size(),unicode,charset,failure,a.size(),b.size(),events.size(),searchEvents.size());std::abort();}
    ++comparisons;changed+=!ia && a!=before;units+=a.size();callbacks+=events.size();interruptions+=ia;
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"",L"|",L"abc",L"{",L"}",L"{x:q}",L"{y:bius}",L"{Y:i}",L"{c:$1234Ab}",L"{C:$0}",L"{f:Arial}",L"{F:}",L"{s:23.5}",L"{S:0}",L"{h:128}",L"{H:0}",L"{o:-3,4}",L"{O:2;0}",L"{\\fe128}",L"{\\fe}",L"{y:BIs}",L"{f:unterminated"};
    const unsigned base=sizeof(pieces)/sizeof(*pieces);const int charsets[]={0,1,128,129,130,134,136,-1};
    unsigned combinations=1;
    for(unsigned len=0;len<=3;++len){if(len)combinations*=base;for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;for(unsigned i=0;i<len;++i){input+=pieces[code%base];code/=base;}compare(input,false,charsets[value%8]);compare(input,true,0);}}
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring input=L"{y:i}a";input.push_back(wchar_t(unit));input+=L"|{f:";input.push_back(wchar_t(unit));input+=L"}z|";compare(input,false,128);compare(input,true,0);}
    uint32_t random=0x32458671;
    for(unsigned value=0;value<16000;++value){std::wstring input;
        for(unsigned i=0;i<value%25;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=pieces[random%base];}
        compare(input,value%2==0,charsets[value%8]);
    }
    const std::wstring hooks=L"{y:bius}{c:$123456}{h:128}{f:Arial}{s:24}{o:2,3}one|{Y:i}two|three";
    for(int failure=0;failure<600;++failure){compare(hooks,false,0,failure);compare(hooks,true,128,failure);}
    // Independently select local/global restore for each style bit, including
    // the charset tag's overwrite of COLOR rather than FONTCHARSET.
    const wchar_t* local[]={L"{c:$123456}",L"{f:Arial}",L"{s:24}",L"{y:b}",L"{y:i}",L"{y:u}",L"{y:s}",L"{h:128}"};
    const wchar_t* global[]={L"{C:$123456}",L"{F:Arial}",L"{S:24}",L"{Y:b}",L"{Y:i}",L"{Y:u}",L"{Y:s}",L"{H:128}"};
    for(unsigned mask=0;mask<256;++mask){std::wstring input;
        for(unsigned i=0;i<8;++i)input+=(mask&(1U<<i))?local[i]:global[i];
        input+=L"x|y||";compare(input,false,0);compare(input,true,0);
    }
    if(!changed || !microSearches || !microSteps || !interruptions || !microTagCallbacks){fprintf(stderr,"MicroDVD route controls failed\n");return 1;}
    printf("%llu exact MicroDVD conversions passed (%llu changed strings, %llu output UTF-16 units, %llu callback observation values, %llu injected interruptions, %llu Rust owners / %llu steps; every UTF-16 value, NUL tails, local/global restore overwrites, unknown/unclosed tags, trailing/empty lines, charset-aware scanning and native tag conversion order; failed numeric scans and unrepresentable arithmetic excluded)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)units,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)microSearches,(unsigned long long)microSteps);
}
