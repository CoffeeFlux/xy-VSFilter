// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_stream.h"
static uint64_t histories=0,operations=0,fields=0,interruptions=0,emptyErrors=0,characters=0;
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
out.insert(out.end(),{error,flags,last});}
struct Outcome {std::vector<int64_t> fields,events;bool operator==(const Outcome& o)const{return fields==o.fields&&events==o.events;}};
template<class Input>static Outcome execute(const std::vector<int>& input,unsigned code,int failure,bool count){
    SsfContext context;context.input=input;context.fail=failure;ssfContext=&context;Input stream;Outcome result;
    for(unsigned step=0;step<12;++step){
        const unsigned operation=(code>>(step%8)*3)%6;int value=0,exception=0;seed();
        try{switch(operation){case 0:value=stream.PeekChar();break;case 1:value=stream.GetChar();break;case 2:value=stream.SkipWhiteSpace(NULL);break;case 3:value=stream.SkipWhiteSpace(L";,/");break;case 4:value=stream.SkipWhiteSpace(L"");break;case 5:value=stream.PopChar();break;}}
        catch(const SsfInterrupted& e){exception=e.kind;}
        effects(result.fields);
        const size_t queued=stream.m_queue.GetCount();ssfRequire(queued<=2);
        result.fields.insert(result.fields.end(),{operation,value,exception,stream.m_line,stream.m_col,stream.m_encoding,static_cast<int64_t>(queued),static_cast<int64_t>(context.position),context.attempts});
        if(queued)result.fields.insert(result.fields.end(),{stream.m_queue.GetHead(),stream.m_queue.GetTail()});
        if(count){++operations;interruptions+=exception==2;emptyErrors+=exception==1;characters+=exception==0&&value!=-1;}
    }
    result.events=context.events;return result;
}
static void compare(const std::vector<int>& input,unsigned code,int failure){
    const auto a=execute<ReferenceSsf::InputStream>(input,code,failure,true);
    const auto b=execute<CandidateSsf::InputStream>(input,code,failure,false);
    if(!(a==b)){fprintf(stderr,"SSF stream mismatch history=%llu code=%u failure=%d\n",(unsigned long long)histories,code,failure);std::abort();}
    ++histories;fields+=a.fields.size()+a.events.size();
}
int main(){
    const char* texts[]={"", "/", "//", "//x", "//\nZ", "/*", "/**/Z", "/*x*/Z", "/ /* q */ //q\nZ", "a//b\nc", "a/*b*/c", "//x\rY", " /**/ //x\nZ", "\t\r\n ;,Z", "a/b*c", "/***/Z", "/*/**/*/Z", "///*x*/\nA"};
    unsigned random=0x71af8392;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(const char* text:texts){std::vector<int> input;for(const char* p=text;*p;++p)input.push_back(static_cast<unsigned char>(*p));for(unsigned i=0;i<128;++i)for(int failure:{-1,0,1,2,5})compare(input,next(),failure);}
    for(int unit:{-1,0,9,10,13,32,47,42,0x85,0xa0,0x2003,0xd800,0xffff})for(unsigned code:{0U,0x249249U,0x492492U,0x6db6dbU,0x924924U,0xb6db6dU})for(int failure:{-1,0,1,2})compare({unit,'/', '*', unit, '*','/',unit,'Z'},code,failure);
    for(unsigned i=0;i<4000;++i){std::vector<int> input;const int alphabet[]={0,9,10,13,32,47,42,65,59,44,0xa0,0xd800,0xffff};for(unsigned j=0;j<i%65;++j)input.push_back(alphabet[next()%13]);compare(input,next(),i%3? -1:static_cast<int>(i%8));}
    ssfRequire(histories&&operations&&interruptions&&emptyErrors&&characters&&ssfOwners==histories&&ssfOwners==ssfReleases&&ssfPushes&&ssfPops);
    printf("%llu exact SSF lookahead histories / %llu operations / %llu character, queue, callback and error-state fields passed (%llu provider interruptions / %llu empty-pop exceptions / %llu non-EOS results / %llu Rust queue owners and releases / %llu pushes / %llu pops; pinned PushChar/PopChar/PeekChar/GetChar/SkipWhiteSpace, safe Rust peek/comment state transitions, native whitespace control flow, repeated and nonsticky EOS, NUL and raw UTF-16 units, partial lookahead after provider exceptions, line/column state and errno/FP/Windows error state; NextChar provider and ThrowError boundary controlled, native ATL queue on Windows and portable deque elsewhere; encoding, actual IO/SSF rendering, allocation failure, non-EOS negative characters, counter overflow and unbounded recursion excluded)\n",(unsigned long long)histories,(unsigned long long)operations,(unsigned long long)fields,(unsigned long long)interruptions,(unsigned long long)emptyErrors,(unsigned long long)characters,(unsigned long long)ssfOwners,(unsigned long long)ssfPushes,(unsigned long long)ssfPops);
}
