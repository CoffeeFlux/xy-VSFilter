// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_bom.h"
static uint64_t histories=0,fields=0,interruptions=0,unknown=0,decoded=0,retries=0,excluded=0;
struct BomResult {std::vector<int64_t> values,events;bool operator==(const BomResult& b)const{return values==b.values&&events==b.events;}};
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
template<class Stream>static BomResult execute(const std::vector<int>& input,int fail,bool count){
    SsfContext context;context.input=input;context.fail=fail;ssfContext=&context;Stream stream;BomResult result;
    for(unsigned attempt=0;attempt<2;++attempt){
        seed();int value=0,error=0;
        try{value=stream.NextChar();}catch(const SsfInterrupted& e){error=e.kind;}
        effects(result.values);result.values.insert(result.values.end(),{value,error,stream.m_encoding,stream.m_line,stream.m_col,static_cast<int64_t>(context.position),context.attempts,static_cast<int64_t>(stream.m_queue.GetCount())});
        if(count){interruptions+=error==2;unknown+=error==3;decoded+=error==0;retries+=attempt==1;}
        // Unknown encoding retries throw before another provider call. Do not
        // feed unbounded EOF into the unrelated native UTF-8 decode loop.
        if(stream.m_encoding!=Stream::unknown)break;
    }
    result.events=context.events;return result;
}
static void compare(const std::vector<int>& input,int fail){
    if(input==std::vector<int>({0xef,0xbb,0xbf})){++excluded;return;}
    const auto a=execute<ReferenceSsf::InputStream>(input,fail,true),b=execute<CandidateSsf::InputStream>(input,fail,false);
    if(!(a==b)){fprintf(stderr,"SSF BOM mismatch history=%llu fail=%d\n",(unsigned long long)histories,fail);std::abort();}
    ++histories;fields+=a.values.size()+a.events.size();
}
int main(){
    for(int second=0;second<=255;++second)for(int third=0;third<=255;++third)for(int fail:{-1,0,1,2,3})compare({0xef,second,third,65,0,0},fail);
    for(int unit=0;unit<=0xffff;++unit){
        for(int fail:{-1,0})compare({unit,0,65,0},fail);
        for(int first:{0xff,0xfe})for(int fail:{-1,0,1})compare({first,unit,65,0,65,0},fail);
    }
    for(int ascii=0;ascii<128;++ascii)for(int fail:{-1,0,1,2,3})compare({0xef,0xbb,0xbf,ascii},fail);
    for(int unit=0;unit<=0xffff;++unit)for(int first:{0xff,0xfe})for(int fail:{-1,2,3})
        compare({first,first==0xff?0xfe:0xff,unit&255,unit>>8},fail);
    for(const auto& input:std::vector<std::vector<int>>{{0xff,0xfe,65},{0xfe,0xff,65},{0xff,0xfe,-1,65},{0xfe,0xff,65,-1}})compare(input,-1);
    for(const auto& input:std::vector<std::vector<int>>{{},{0xef},{0xef,0xbb},{0xff},{0xfe},{0xff,0xfe},{0xfe,0xff},{0xef,0xbb,0xbf}})
        compare(input,-1);
    ssfRequire(histories&&interruptions&&unknown&&decoded&&retries&&ssfBomSteps&&ssfUtf16Steps&&excluded==1);
    printf("%llu exact SSF BOM histories / %llu byte, callback, encoding and ambient-error fields passed (%llu provider interruptions / %llu unknown-encoding exceptions / %llu decoded results / %llu retained-unknown retries / %llu Rust scalar BOM transitions / %llu Rust UTF-16 results; one known UTF-8 EOF history excluded before both implementations and NOT a pass; complete pinned NextChar, every two-byte tail after EF, every UTF-16-valued first/second provider result, partial BOMs, exact short-circuit reads and exception state, all UTF-16 values in both native byte-order branches and every ASCII UTF-8 value; UTF-8 decoding remains native, arbitrary UTF-8 decode prefixes/overflow, provider reentry/mutation, actual IO and visible SSF rendering excluded)\n",(unsigned long long)histories,(unsigned long long)fields,(unsigned long long)interruptions,(unsigned long long)unknown,(unsigned long long)decoded,(unsigned long long)retries,(unsigned long long)ssfBomSteps,(unsigned long long)ssfUtf16Steps);
}
