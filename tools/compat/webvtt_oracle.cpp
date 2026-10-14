// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_webvtt.h"
struct Outcome {
    std::vector<uint16_t> text;std::vector<std::vector<uint16_t>> events;int attempts;bool interrupted,mutated;
    bool operator==(const Outcome& o)const{return text==o.text && events==o.events && attempts==o.attempts && interrupted==o.interrupted && mutated==o.mutated;}
};
static uint64_t comparisons=0,changes=0,interruptions=0,mutations=0,operations=0;
static Outcome execute(const std::wstring& input,bool candidate,int failure,bool mutate){
    CueString text(input.data(),static_cast<int>(input.size()));cueFailure=failure;cueAttempts=0;cueMutate=mutate;cueMutated=false;cueEvents.clear();
    Outcome result;result.interrupted=false;
    try{if(candidate)CandidateWebVTT::WebVTTCueStrip(text);else ReferenceWebVTT::WebVTTCueStrip(text);}catch(const CueInterrupted&){result.interrupted=true;}
    for(int i=0;i<text.GetLength();++i)result.text.push_back(static_cast<uint16_t>(text[i]));
    result.events=cueEvents;result.attempts=cueAttempts;result.mutated=cueMutated;return result;
}
static void compare(const std::wstring& input,int failure=-1,bool mutate=false){
    auto a=execute(input,false,failure,mutate),b=execute(input,true,failure,mutate);
    std::vector<uint16_t> before;for(auto c:input)before.push_back(static_cast<uint16_t>(c));
    if(!(a==b)){fprintf(stderr,"Cue-strip mismatch case=%llu length=%zu failure=%d mutate=%d\n",(unsigned long long)comparisons,input.size(),failure,mutate);std::abort();}
    ++comparisons;changes+=a.text!=before;interruptions+=a.interrupted;mutations+=a.mutated;operations+=a.attempts;
}
int main(){
    const std::wstring prefixes[]={L"align:",L"position:",L"vertical:",L"line:",L"size:",L"Align:",L"LINE:",L" align:",L"align",L"size",L"",L"x"};
    const std::wstring tails[]={L"\ntext",L"\n \ttext",L"\r\ntext",L"\n\nsize:x\ntext",L"text",L"\n",L"",std::wstring(L"\0\ntext",6),std::wstring(L"\n \0text",7)};
    for(const auto& prefix:prefixes)for(const auto& tail:tails)for(unsigned cut=0;cut<=prefix.size();++cut){
        auto input=prefix.substr(0,cut)+tail;compare(input);compare(input,0);compare(input,1);compare(input,-1,true);
    }
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring input=L"align:x\n";input.push_back(wchar_t(unit));input+=L" tail";compare(input);}
    uint32_t random=0x81469235;
    for(unsigned value=0;value<15000;++value){
        random^=random<<13;random^=random>>17;random^=random<<5;std::wstring input=prefixes[random%12];
        for(unsigned i=0;i<value%23;++i){random^=random<<13;random^=random>>17;random^=random<<5;input.push_back(wchar_t(random%65536));}
        input+=tails[value%9];compare(input,-1,value%7==0);
    }
    const auto control=execute(L"size:x\n\ttext",true,-1,false);
    const std::vector<uint16_t> expected={116,101,120,116};
    if(control.text!=expected || !changes || !interruptions || !mutations){fprintf(stderr,"Cue-strip controls failed\n");return 1;}
    printf("%llu exact WebVTT cue-prefix comparisons passed (%llu changed strings, %llu native Delete/TrimLeft operations, %llu injected interruptions, %llu strings changed between deletion and trimming; exact leading keywords, first newline/NUL, every post-newline code unit, partial strings and live native whitespace handling)\n",(unsigned long long)comparisons,(unsigned long long)changes,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)mutations);
}
