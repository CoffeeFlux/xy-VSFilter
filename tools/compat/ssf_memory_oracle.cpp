// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_memory.h"
static uint64_t histories=0,reads=0,fields=0,exceptions=0,copies=0,borrowed=0,adopted=0;
struct MemoryResult {std::vector<int64_t> values,events,errors;bool operator==(const MemoryResult& b)const{return values==b.values&&events==b.events&&errors==b.errors;}};
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static void effects(std::vector<int64_t>& values){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
    const unsigned long last=GetLastError();
#else
    const unsigned long last=0;
#endif
    values.insert(values.end(),{error,flags,last});
}
template<class Memory>static MemoryResult execute(int length,unsigned pattern,bool copy,bool release,bool decline,bool nullInput,bool count){
    SsfMemoryContext context;context.decline=decline;ssfMemoryContext=&context;SsfContext errors;ssfContext=&errors;MemoryResult result;
    const int backing=length>4?length:4;BYTE* input=nullInput?NULL:ssfMemoryNew(backing);
    if(input)for(int i=0;i<backing;++i)input[i]=static_cast<BYTE>(pattern+73*i);
    seed();
    try{
        {
            Memory stream(input,length,copy,release);effects(result.values);
            result.values.insert(result.values.end(),{stream.m_pos,stream.m_len,stream.m_fFree,stream.m_pBytes==input});
            if(count){copies+=copy;borrowed+=!copy&&!release;adopted+=!copy&&release;}
            const int steps=(length>0?length:0)+4;
            for(int step=0;step<steps;++step){
                if(step%5==0)input[step%backing]^=0xa5;
                seed();const int value=stream.NextByte();effects(result.values);
                result.values.insert(result.values.end(),{value,stream.m_pos,stream.m_len,stream.m_fFree,stream.m_line,stream.m_col});
                if(count)++reads;
            }
            for(int i=0;i<backing;++i)result.values.push_back(input[i]);
            seed();
        }
        effects(result.values);
    }catch(const SsfInterrupted& exception){effects(result.values);result.values.push_back(exception.kind);if(count)++exceptions;}
    if(input&&(copy||!release))ssfMemoryDelete(input);
    ssfRequire(context.allocations.empty());result.events=context.events;result.errors=errors.events;
    ssfMemoryContext=NULL;ssfContext=NULL;return result;
}
static void compare(int length,unsigned pattern,bool copy,bool release,bool decline,bool nullInput=false){
    const auto a=execute<ReferenceSsf::MemoryInputStream>(length,pattern,copy,release,decline,nullInput,true);
    const auto b=execute<CandidateSsf::MemoryInputStream>(length,pattern,copy,release,decline,nullInput,false);
    if(!(a==b)){fprintf(stderr,"SSF memory mismatch length=%d pattern=%u copy=%d release=%d decline=%d null=%d\n",length,pattern,copy,release,decline,nullInput);std::abort();}
    ++histories;fields+=a.values.size()+a.events.size()+a.errors.size();
}
int main(){
    for(int length:{-9,-1,0,1,2,3,7,16,31,64,127,256,257})for(unsigned pattern=0;pattern<256;++pattern)
        for(bool copy:{false,true})for(bool release:{false,true})for(bool decline:{false,true}){
            if(copy&&length<0)continue; // Native negative allocation counts are outside this fixture.
            compare(length,pattern,copy,release,decline);
        }
    for(int length:{-1,0,1,4})for(bool release:{false,true})compare(length,0,false,release,false,true);
    ssfRequire(histories&&reads&&exceptions&&copies&&borrowed&&adopted&&ssfMemoryOwners&&ssfMemoryOwners==ssfMemoryReleases&&ssfMemoryReads&&ssfMemoryDeclines);
    printf("%llu exact SSF memory-stream histories / %llu NextByte calls / %llu state, byte, allocation and ambient-error fields passed (%llu copy-mode histories / %llu borrowed / %llu adopted / %llu null-pointer exceptions / %llu Rust snapshot owners and releases / %llu Rust byte reads / %llu modeled Rust declines; whole pinned constructors, destructor and NextByte; every byte value, source mutation after copying versus live borrowed/adopted input, zero-length native allocation, borrowed negative lengths, repeated EOF, pointer alias relationships and release order; guarded allocations, NextChar encoding, actual IO, native allocation failure, invalid copy spans and negative copy counts excluded)\n",(unsigned long long)histories,(unsigned long long)reads,(unsigned long long)fields,(unsigned long long)copies,(unsigned long long)borrowed,(unsigned long long)adopted,(unsigned long long)exceptions,(unsigned long long)ssfMemoryOwners,(unsigned long long)ssfMemoryReads,(unsigned long long)ssfMemoryDeclines);
}
