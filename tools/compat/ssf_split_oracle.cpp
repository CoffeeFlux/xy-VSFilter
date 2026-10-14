// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_split.h"
static uint64_t histories=0,fields=0,tokens=0,interruptions=0,aliases=0;
struct SplitResult {std::vector<int64_t> values,events;bool operator==(const SplitResult& b)const{return values==b.values&&events==b.events;}};
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static void effects(std::vector<int64_t>& out){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
    const unsigned long last=GetLastError();
#else
    const unsigned long last=0;
#endif
    out.insert(out.end(),{error,flags,last});
}
template<class Split>static void state(Split& value,std::vector<int64_t>& out,bool count){
    out.insert(out.end(),{static_cast<int64_t>(value.GetCount()),static_cast<int64_t>(static_cast<size_t>(value))});
    for(size_t i=0;i<value.GetCount();++i){const CStringW& part=value.GetAt(i);out.push_back(part.GetLength());for(int j=0;j<part.GetLength();++j)out.push_back(part[j]);if(count)++tokens;}
}
template<class Split>static SplitResult execute(const std::vector<WCHAR>& input,const std::vector<WCHAR>& separator,size_t limit,int type,int fail,int alias,bool single,bool count){
    SplitContext context;splitContext=&context;SplitResult result;
    CStringW source(input.empty()?splitEmpty:input.data(),static_cast<int>(input.size()));
    std::vector<WCHAR> terminator(separator);terminator.push_back(0);
    LPCWSTR sep=alias<0?terminator.data():source.GetString()+alias;
    seed();
    {
        std::unique_ptr<Split> split(single?new Split(terminator[0],source,limit,static_cast<typename Split::SplitType>(type)):new Split(sep,source,limit,static_cast<typename Split::SplitType>(type)));
        effects(result.values);state(*split,result.values,count);
        context.fail=fail;context.attempt=0;seed();bool exception=false;
        try{split->DoSplit(sep,source,limit,static_cast<typename Split::SplitType>(type));}catch(const SplitInterrupted&){exception=true;if(count)++interruptions;}
        effects(result.values);result.values.push_back(exception);state(*split,result.values,count);
        context.fail=-1;seed();split->DoSplit(splitEmpty,source,limit,static_cast<typename Split::SplitType>(type));effects(result.values);state(*split,result.values,count);
        for(int i=0;i<source.GetLength();++i)result.values.push_back(source[i]);
    }
    result.events=context.events;splitContext=NULL;return result;
}
static void compare(const std::vector<WCHAR>& input,const std::vector<WCHAR>& separator,size_t limit,int type,int fail=-1,int alias=-1,bool single=false){
    const auto a=execute<ReferenceSplit::Split>(input,separator,limit,type,fail,alias,single,true);
    const auto b=execute<CandidateSplit::Split>(input,separator,limit,type,fail,alias,single,false);
    if(!(a==b)){fprintf(stderr,"SSF split mismatch history=%llu length=%zu sep=%zu limit=%zu type=%d fail=%d alias=%d single=%d\n",(unsigned long long)histories,input.size(),separator.size(),limit,type,fail,alias,single);std::abort();}
    ++histories;fields+=a.values.size()+a.events.size();aliases+=alias>=0;
}
int main(){
    for(unsigned unit=0;unit<=0xffff;++unit)for(int type=0;type<3;++type){
        std::vector<WCHAR> input={32,static_cast<WCHAR>(unit),44,0,static_cast<WCHAR>(unit),44,32};
        compare(input,{44},0,type);compare(input,{static_cast<WCHAR>(unit)},2,type,-1,-1,true);compare(input,{},1,type,-1,1);
    }
    const std::vector<std::vector<WCHAR>> inputs={{},{44},{44,44},{32,44,32,44,32},{65,66,65,66,65},{65,0,44,66},{9,32,44,0xa0,44,0x2003},{0xd800,44,0xffff,44,0xdc00}};
    const std::vector<std::vector<WCHAR>> separators={{},{44},{65},{65,66},{65,66,65},{44,44},{0},{44,0,65},{0xd800}};
    for(const auto& input:inputs)for(const auto& separator:separators)for(size_t limit:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(-1)})for(int type=0;type<3;++type)for(int fail:{-1,0,1,2})compare(input,separator,limit,type,fail);
    if(!(histories&&tokens&&interruptions&&aliases&&ssfSplitSearches))std::abort();
    printf("%llu exact SSF split histories / %llu token, array, callback and ambient-error fields passed (%llu observed tokens / %llu interrupted Add calls / %llu separators aliasing input / %llu Rust searches; whole pinned constructors and DoSplit, every UTF-16 unit, embedded NUL, multi-unit and overlapping separators, Min/Def/Max, accepted-token limits, repeated clear/split and partial arrays after Add interruptions; actual Windows CStringW/ATL or portable string/array model; native trimming, array storage and numeric conversion retained, actual allocation failure, old-array separator aliases and unrepresentable lengths excluded)\n",(unsigned long long)histories,(unsigned long long)fields,(unsigned long long)tokens,(unsigned long long)interruptions,(unsigned long long)aliases,(unsigned long long)ssfSplitSearches);
}
