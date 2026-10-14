// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_literal.h"
static uint64_t histories=0,fields=0,commits=0,errors=0,interruptions=0;
struct LiteralResult {
    std::vector<int64_t> values,events;
    int attempts;
    bool operator==(const LiteralResult& b)const{return values==b.values&&events==b.events&&attempts==b.attempts;}
};
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
template<class File>static LiteralResult execute(const std::vector<int>& input,bool block,int fail,bool after,bool skew,bool count){
    LiteralContext context;context.input=input;context.fail=fail;context.after=after;context.skew=skew;literalContext=&context;
    InputStream stream;Definition definition;File file;LiteralResult result;
    for(int retry=0;retry<2;++retry){
        seed();int error=0;
        try{if(block)file.ParseBlock(stream,&definition);else file.ParseQuotedString(stream,&definition);}
        catch(const LiteralInterrupted& e){error=e.kind;}
        effects(result.values);
        result.values.insert(result.values.end(),{error,definition.status,static_cast<int64_t>(definition.value.size()),static_cast<int64_t>(context.position)});
        result.values.insert(result.values.end(),definition.value.begin(),definition.value.end());
        if(count){commits+=error==0;errors+=error==3;interruptions+=error==1||error==2;}
    }
    result.events=context.events;result.attempts=context.attempt;return result;
}
static int compare(const std::vector<int>& input,bool block,int fail=-1,bool after=false,bool skew=false){
    const auto a=execute<ReferenceLiteral::File>(input,block,fail,after,skew,true),b=execute<CandidateLiteral::File>(input,block,fail,after,skew,false);
    if(!(a==b)){fprintf(stderr,"SSF literal mismatch history=%llu block=%d fail=%d after=%d skew=%d\n",(unsigned long long)histories,block,fail,after,skew);for(int c:input)fprintf(stderr,"%d,",c);fprintf(stderr,"\n");std::abort();}
    ++histories;fields+=a.values.size()+a.events.size();return a.attempts;
}
static void faults(const std::vector<int>& input,bool block){
    for(bool skew:{false,true}){
        const int attempts=compare(input,block,-1,false,skew);
        for(int fail=0;fail<attempts;++fail)for(bool after:{false,true})compare(input,block,fail,after,skew);
    }
}
int main(){
    for(int unit=0;unit<=0xffff;++unit){
        for(int quote:{34,39}){
            compare({quote,unit,quote,65},false);
            compare({quote,92,unit,quote,65},false);
        }
        compare({123,unit,125,65},true);
        compare({123,92,unit,125,65},true);
    }
    const int alphabet[]={-1,0,10,13,34,39,92,123,125,65};
    for(int length=0,lengthCases=1;length<=4;++length,lengthCases*=10){
        for(int code=0;code<lengthCases;++code){
            std::vector<int> tail;int n=code;
            for(int i=0;i<length;++i,n/=10)tail.push_back(alphabet[n%10]);
            for(bool block:{false,true}){
                auto input=tail;input.insert(input.begin(),block?123:34);
                compare(input,block);
                if(length<=2)faults(input,block);
            }
        }
    }
    for(const auto& input:std::vector<std::vector<int>>{{},{-1},{0},{65},{34,65,92,10,65,34},{39,65,10,39},{123,123,92,125,125,125},{32,9,34,0,0xd800,0xdc00,34},{32,58,61,123,0,125},{34,0x10000,0x1ffff,34},{123,0x10000,0x1ffff,125},{34,-2,34},{123,-2,125}})
        for(bool block:{false,true})faults(input,block);
    for(int depth:{1,2,63,1024}){
        std::vector<int> input(depth+1,123);input.push_back(65);input.insert(input.end(),depth+1,125);
        compare(input,true);
    }
    if(!histories||!commits||!errors||!interruptions||!literalSteps)std::abort();
    printf("%llu exact SSF quoted-string/block histories / %llu callback, destination and ambient-error fields passed (%llu completed parses / %llu native parse errors / %llu before-or-after callback interruptions / %llu Rust transitions; whole pinned methods, all UTF-16 values direct and escaped, both quote styles, nested braces, embedded NUL/surrogates, raw provider values, differing Peek/Get including transient EOS, repeated calls and partial commit state; actual Windows CStringW append or portable string boundary, controlled stream/definition/error callbacks; allocation failure, depth beyond native integer range, locale/comment handling within the controlled stream and actual IO excluded)\n",(unsigned long long)histories,(unsigned long long)fields,(unsigned long long)commits,(unsigned long long)errors,(unsigned long long)interruptions,(unsigned long long)literalSteps);
}
