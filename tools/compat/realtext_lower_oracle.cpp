// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
#include <clocale>
struct Outcome {
    std::wstring input,output;std::vector<int64_t> events;int attempts;bool interrupted,mutated;
    bool operator==(const Outcome& o)const{return input==o.input && output==o.output && events==o.events && attempts==o.attempts && interrupted==o.interrupted && mutated==o.mutated;}
};
static Outcome* active=nullptr;static int failureIndex=-1,mutationMode=0;
static void observe(wint_t unit,wint_t result){
    active->events.insert(active->events.end(),{int64_t(unit),int64_t(result),int64_t(active->input.size())});for(wchar_t c:active->input)active->events.push_back(c);
    if(active->attempts++==failureIndex)throw RealTextInterrupted();
    if(!active->mutated && mutationMode){
        if(mutationMode==1)active->input.clear();
        if(mutationMode==2)active->input+=L" ADDED";
        if(mutationMode==3 && active->input.size()>1)active->input[1]=L'Z';
        active->mutated=true;
    }
}
template<class Parser> static Outcome execute(const std::wstring& input,int failure,int mutation){
    Parser parser;Outcome result={};result.input=input;result.output=L"sentinel";active=&result;failureIndex=failure;mutationMode=mutation;realTextLowerHook=observe;
    try{result.output=parser.StringToLower(result.input);}catch(const RealTextInterrupted&){result.interrupted=true;}
    realTextLowerHook=nullptr;active=nullptr;return result;
}
static uint64_t comparisons=0,operations=0,interruptions=0,mutations=0,changed=0,locales=0;
static void compare(const std::wstring& input,int failure=-1,int mutation=0){
    const auto a=execute<ReferenceRealText::CRealTextParser>(input,failure,mutation),b=execute<CandidateRealText::CRealTextParser>(input,failure,mutation);
    if(!(a==b)){fprintf(stderr,"RealText lowercase mismatch case=%llu length=%zu failure=%d mutation=%d\n",(unsigned long long)comparisons,input.size(),failure,mutation);std::abort();}
    ++comparisons;operations+=a.attempts;interruptions+=a.interrupted;mutations+=a.mutated;changed+=!a.interrupted && a.output!=input;
}
int main(){
    // Compare within each available native locale, never against a Rust Unicode
    // table or results from another OS. Locale aliases can share the same table.
    for(const char* locale:{"C","","en_US.UTF-8",".1252",".UTF-8"}){
        if(!std::setlocale(LC_CTYPE,locale))continue;++locales;
        for(unsigned value=0;value<=0xffff;++value){std::wstring input=L"A";input.push_back(wchar_t(value));input+=L"Z";compare(input);}
    }
    std::setlocale(LC_CTYPE,"C");
    const wchar_t units[]={L'A',L'z',L' ',0,wchar_t(0xd800),wchar_t(0xffff)};unsigned combinations=1;
    for(unsigned length=0;length<=5;++length){if(length)combinations*=6;for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;for(unsigned i=0;i<length;++i){input.push_back(units[code%6]);code/=6;}compare(input);}}
    uint32_t random=0x88351725;
    for(unsigned value=0;value<12000;++value){std::wstring input;for(unsigned i=0;i<value%41;++i){random^=random<<13;random^=random>>17;random^=random<<5;input.push_back(wchar_t(random&0xffff));}compare(input,-1,value%11==0?int(value%4):0);}
    const std::wstring samples[]={L"",L"ABC",L"I FONT",std::wstring(L"A\0Z",3),std::wstring(1,wchar_t(0xdfff))};
    for(const auto& input:samples)for(int failure=-1;failure<14;++failure)for(int mutation=0;mutation<4;++mutation)compare(input,failure,mutation);
    const auto control=execute<CandidateRealText::CRealTextParser>(std::wstring(L"A\0Z",3),-1,0);
    if(control.output!=std::wstring(L"a\0z",3) || !locales || !interruptions || !mutations || !changed)return 1;
    printf("%llu exact RealText lowercase comparisons passed (%llu available locale selections, %llu changed outputs, %llu native case conversions, %llu injected interruptions, %llu live input mutations; every UTF-16 value, embedded NULs, original classification order and untouched caller output after exceptions)\n",(unsigned long long)comparisons,(unsigned long long)locales,(unsigned long long)changed,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)mutations);
}
