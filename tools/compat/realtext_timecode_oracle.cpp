// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
#include <climits>
#include <stdexcept>
struct Outcome {
    std::wstring input;std::vector<int64_t> conversions;std::string error;int value,attempts,exception;bool mutated;
    bool operator==(const Outcome& o)const{return input==o.input && conversions==o.conversions && error==o.error && value==o.value && attempts==o.attempts && exception==o.exception && mutated==o.mutated;}
};
static uint64_t comparisons=0,excluded=0,nonzero=0,interruptions=0,bounds=0,mutations=0,conversions=0;
// Limit differential execution to representable native arithmetic. Conversion
// itself remains the actual host CRT operation, including malformed text.
static bool representable(const std::wstring& input){
    int64_t sum=0,multiplier=input.find_first_of(L".:")==std::wstring::npos?1000:1;
    size_t end=input.size();
    for(;;){size_t begin=end;while(begin && input[begin-1]!=L'.' && input[begin-1]!=L':')--begin;
        const bool final=begin==0;std::wstring part=input.substr(begin,end-begin);
        if(!final && multiplier==1)while(part.size()<3)part+=L"0";
        int64_t product=multiplier*realTextToInt(part.c_str());if(product<INT_MIN || product>INT_MAX)return false;
        sum+=product;if(sum<INT_MIN || sum>INT_MAX)return false;
        if(final)return true;
        multiplier=multiplier==1?1000:multiplier*60;if(multiplier<INT_MIN || multiplier>INT_MAX)return false;
        end=begin-1;
    }
}
template<class Parser> static Outcome execute(const std::wstring& input,int failure,int mutation){
    Parser parser;Outcome result;result.input=input;result.value=INT_MIN;result.exception=0;
    realTextTimecodeInput=&result.input;realTextTimecodeFailure=failure;realTextTimecodeAttempts=0;realTextTimecodeMutation=mutation;realTextTimecodeMutated=false;realTextConversions.clear();
    try{result.value=parser.GetTimecode(result.input);}catch(const RealTextInterrupted&){result.exception=1;}catch(const std::out_of_range& e){result.exception=2;result.error=e.what();}
    result.conversions=realTextConversions;result.attempts=realTextTimecodeAttempts;result.mutated=realTextTimecodeMutated;realTextTimecodeInput=nullptr;return result;
}
static void compare(const std::wstring& input,int failure=-1,int mutation=0){
    if(!representable(input)){++excluded;return;}
    if(mutation==1 && !input.empty()){auto modified=input;modified[0]=L'3';if(!representable(modified)){++excluded;return;}}
    auto a=execute<ReferenceRealText::CRealTextParser>(input,failure,mutation),b=execute<CandidateRealText::CRealTextParser>(input,failure,mutation);
    if(!(a==b)){fprintf(stderr,"RealText timecode mismatch case=%llu length=%zu failure=%d mutation=%d reference=%d candidate=%d exceptions=%d/%d\n",(unsigned long long)comparisons,input.size(),failure,mutation,a.value,b.value,a.exception,b.exception);std::abort();}
    ++comparisons;nonzero+=a.exception==0 && a.value!=0;interruptions+=a.exception==1;bounds+=a.exception==2;mutations+=a.mutated;conversions+=a.attempts;
}
int main(){
    const wchar_t alphabet[]={L'0',L'1',L'9',L'.',L':',L'-',L'x',0};unsigned combinations=1;
    for(unsigned length=0;length<=6;++length){
        if(length)combinations*=8;
        for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;
            for(unsigned i=0;i<length;++i){input.push_back(alphabet[code%8]);code/=8;}
            compare(input);
        }
    }
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring input=L"1:";input.push_back(wchar_t(unit));input+=L".2";compare(input);}
    const std::wstring samples[]={L"1:2",L"1::2.3",L"10",L":12",L"12:",L"",L"1.2.3.4",L"0:0:0:0:0",L"00:01.05",L" 1: -2",L"2147483",L"1.x"};
    for(const auto& input:samples)for(int failure=-1;failure<8;++failure)for(int mutation=0;mutation<=3;++mutation)compare(input,failure,mutation);
    const auto control=execute<CandidateRealText::CRealTextParser>(L"1:2",-1,0);
    const auto changed=execute<CandidateRealText::CRealTextParser>(L"1:2",-1,1);
    if(control.value!=1200 || changed.value!=3200 || !nonzero || !interruptions || !bounds || !mutations || !excluded){fprintf(stderr,"RealText timecode controls failed\n");return 1;}
    printf("%llu exact RealText timecode comparisons passed (%llu nonzero results, %llu native numeric conversions, %llu injected conversion interruptions, %llu matching checked-string exceptions after input shrink, %llu callback-mutated inputs, %llu arithmetic-domain exclusions; reverse fields, colon-only fractional behavior, trailing-zero padding, raw NULs, cached initial length and live remaining input)\n",(unsigned long long)comparisons,(unsigned long long)nonzero,(unsigned long long)conversions,(unsigned long long)interruptions,(unsigned long long)bounds,(unsigned long long)mutations,(unsigned long long)excluded);
}
