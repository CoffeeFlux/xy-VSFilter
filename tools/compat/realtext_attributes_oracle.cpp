// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
#include <climits>
struct Outcome {
    std::wstring input;std::map<std::wstring,std::wstring> attributes;std::vector<int64_t> events,conversions;unsigned position;int attempts;bool returned,interrupted,mutated;
    bool operator==(const Outcome& o)const{return input==o.input && attributes==o.attributes && events==o.events && conversions==o.conversions && position==o.position && attempts==o.attempts && returned==o.returned && interrupted==o.interrupted && mutated==o.mutated;}
};
static uint64_t comparisons=0,successful=0,interruptions=0,mutations=0,operations=0,changed=0;
template<class Parser> static Outcome execute(const std::wstring& input,unsigned start,bool ignore,int failure,int mutation){
    Parser parser;parser.m_bTryToIgnoreErrors=ignore;Outcome result;result.input=input;result.attributes={{L"prior",L"old"},{L"a",L"original"}};result.position=start;result.returned=false;result.interrupted=false;
    realTextInput=&result.input;realTextPosition=&result.position;realTextAttributes=&result.attributes;realTextFailure=failure;realTextAttempts=0;realTextMutation=mutation;realTextMutated=false;realTextConversions.clear();realTextAttributeEvents.clear();
    try{result.returned=parser.GetAttributes(result.input,result.position,result.attributes);}catch(const RealTextInterrupted&){result.interrupted=true;}
    result.events=realTextAttributeEvents;result.conversions=realTextConversions;result.attempts=realTextAttempts;result.mutated=realTextMutated;realTextInput=nullptr;return result;
}
static void compare(const std::wstring& input,unsigned start=0,bool ignore=true,int failure=-1,int mutation=0){
    const auto a=execute<ReferenceRealText::CRealTextParser>(input,start,ignore,failure,mutation);const auto b=execute<CandidateRealText::CRealTextParser>(input,start,ignore,failure,mutation);
    if(!(a==b)){fprintf(stderr,"RealText attribute mismatch case=%llu length=%zu start=%u ignore=%d failure=%d mutation=%d\n",(unsigned long long)comparisons,input.size(),start,ignore,failure,mutation);std::abort();}
    ++comparisons;successful+=a.returned;interruptions+=a.interrupted;mutations+=a.mutated;operations+=a.attempts;changed+=a.attributes!=std::map<std::wstring,std::wstring>{{L"prior",L"old"},{L"a",L"original"}};
}
int main(){
    const wchar_t units[]={L'a',L'=',L' ',L'\'',L'"',L'/',L'>',0};unsigned combinations=1;
    for(unsigned length=0;length<=5;++length){
        if(length)combinations*=8;
        for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;
            for(unsigned i=0;i<length;++i){input.push_back(units[code%8]);code/=8;}
            compare(input,0,false);compare(input,0,true);
        }
    }
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring input=L"a='";input.push_back(wchar_t(unit));input+=L"' b=tail/>";compare(input);}
    uint32_t random=0x75132894;const std::wstring parts[]={L"A",L"x",L"size",L"=",L"=missing",L"'",L"\"",L"\n",L"\r",L"\t",L" ",L"/>",L">",L"abc",std::wstring(1,0)};
    for(unsigned value=0;value<12000;++value){std::wstring input;
        for(unsigned i=0;i<value%19;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=parts[random%15];}
        compare(input,unsigned(random%(input.size()+3)),value%2!=0,-1,value%7==0?1:0);
    }
    const std::wstring samples[]={L" a='one' A=two/>",L"x y=one z='two\" q=unfinished",L"a=1 b=2 c=3/>",L"a='\n\t text' />"};
    for(const auto& input:samples)for(unsigned start:{0u,1u,UINT_MAX})for(bool ignore:{false,true})for(int mutation:{0,1,2})for(int failure=-1;failure<20;++failure)compare(input,start,ignore,failure,mutation);
    const auto control=execute<CandidateRealText::CRealTextParser>(L"a='one\" b=unfinished",0,true,-1,0);
    if(control.returned || control.attributes.at(L"a")!=L"one" || control.position!=20 || !mutations || !interruptions || !successful || !changed){fprintf(stderr,"RealText attribute controls failed position=%u\n",control.position);return 1;}
    printf("%llu exact RealText attribute comparisons passed (%llu successful returns, %llu changed maps, %llu native whitespace/lowercase operations, %llu injected interruptions, %llu live input mutations; mismatched quotes, whitespace inside quotes, missing equals recovery, partial map/cursor state, duplicate names, NULs and every UTF-16 value)\n",(unsigned long long)comparisons,(unsigned long long)successful,(unsigned long long)changed,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)mutations);
}
