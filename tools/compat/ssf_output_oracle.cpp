// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_stream.h"
static uint64_t histories=0,operations=0,fields=0,callbacks=0,interruptions=0,reentries=0,mutations=0;
struct OutputResult {std::vector<int64_t> trace;bool operator==(const OutputResult& other)const{return trace==other.trace;}};
template<class Base> class Sink:public Base {
public:
    OutputResult result;
    int attempt=0,fail,trigger,mode,newEncoding,depth=0;
    bool count,failAfter;
    Sink(int encoding,bool first,int failure,int triggerAt,int behavior,int changed,bool after,bool counting)
        :Base(static_cast<typename Base::encoding_t>(encoding)),fail(failure),trigger(triggerAt),mode(behavior),newEncoding(changed),count(counting),failAfter(after){this->m_bof=first;}
    void NextByte(int value){
        const int current=attempt++;
        result.trace.insert(result.trace.end(),{1,current,value,this->m_bof,this->m_encoding,depth});
        if(count)++callbacks;
        if(current==fail&&!failAfter)throw SsfInterrupted{2};
        if(current==trigger){
            trigger=-1;
            if(mode&1){this->m_encoding=static_cast<typename Base::encoding_t>(newEncoding);if(count)++mutations;}
            if(mode&2){
                struct Depth {int& value;Depth(int& v):value(v){++value;}~Depth(){--value;}} guard(depth);
                if(count)++reentries;
                this->PutChar(0xd800);
            }
        }
        result.trace.insert(result.trace.end(),{2,current,value,this->m_bof,this->m_encoding,depth});
        if(current==fail&&failAfter)throw SsfInterrupted{2};
    }
};
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
template<class Base>static OutputResult execute(unsigned unit,int encoding,bool first,int fail,int trigger,int mode,int changed,bool after,bool count){
    Sink<Base> output(encoding,first,fail,trigger,mode,changed,after,count);
    for(unsigned c:{unit,unit,0U,0xffffU}){
        seed();int exception=0;
        try{output.PutChar(static_cast<WCHAR>(c));}catch(const SsfInterrupted& e){exception=e.kind;}
        const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
        const unsigned long last=GetLastError();
#else
        const unsigned long last=0;
#endif
        output.result.trace.insert(output.result.trace.end(),{3,c,exception,output.m_bof,output.m_encoding,output.m_line,output.m_col,output.attempt,output.depth,error,flags,last});
        if(count){++operations;interruptions+=exception!=0;}
    }
    return output.result;
}
static void compare(unsigned unit,int encoding,bool first,int fail=-1,int trigger=-1,int mode=0,int changed=0,bool after=false){
    const auto a=execute<ReferenceSsf::OutputStream>(unit,encoding,first,fail,trigger,mode,changed,after,true);
    const auto b=execute<CandidateSsf::OutputStream>(unit,encoding,first,fail,trigger,mode,changed,after,false);
    if(!(a==b)){fprintf(stderr,"SSF output mismatch unit=%u encoding=%d first=%d fail=%d trigger=%d mode=%d changed=%d after=%d\n",unit,encoding,first,fail,trigger,mode,changed,after);std::abort();}
    ++histories;fields+=a.trace.size();
}
int main(){
    for(int encoding=0;encoding<6;++encoding)for(unsigned unit=0;unit<=0xffff;++unit)for(bool first:{false,true})compare(unit,encoding,first);
    for(unsigned unit:{0U,0x7fU,0x80U,0x7ffU,0x800U,0xd800U,0xdc00U,0xfeffU,0xfffeU,0xffffU})
        for(int encoding=0;encoding<6;++encoding)for(bool first:{false,true})
            for(int fail:{-1,0,1,2,3,5,9})for(int trigger:{0,1,2,4})
                for(int mode:{1,2,3})for(int changed=0;changed<6;++changed)for(bool after:{false,true})
                    compare(unit,encoding,first,fail,trigger,mode,changed,after);
    ssfRequire(histories&&operations&&callbacks&&interruptions&&reentries&&mutations&&ssfOutputPlans);
    printf("%llu exact SSF output histories / %llu PutChar calls / %llu callback, state and ambient-error fields passed (%llu byte callbacks / %llu interrupted calls / %llu reentrant calls / %llu encoding mutations / %llu Rust scalar output plans; whole pinned PutChar and constructors, all UTF-16 units in all six encodings, first-character BOM and repeated calls, historical left-shift encoding, NUL and surrogate values, partial callback failures before and after output, bounded reentry and encoding changes; virtual NextByte controlled, formatting, actual IO and SSF rendering excluded)\n",(unsigned long long)histories,(unsigned long long)operations,(unsigned long long)fields,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)reentries,(unsigned long long)mutations,(unsigned long long)ssfOutputPlans);
}
