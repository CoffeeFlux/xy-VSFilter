// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
#include <climits>
#include <locale>
struct Outcome {
    std::wstring output,separator,millis;std::vector<int64_t> events;int attempts,status,state,flushes;bool mutated;
    bool operator==(const Outcome& o)const{return output==o.output && separator==o.separator && millis==o.millis && events==o.events && attempts==o.attempts && status==o.status && state==o.state && flushes==o.flushes && mutated==o.mutated;}
};
static Outcome* active=nullptr;static int failureIndex=-1,mutationMode=0;
static void stringEvent(const std::wstring& s){active->events.push_back(s.size());for(wchar_t c:s)active->events.push_back(c);}
static void observe(int kind,int64_t value){
    if(!active)return;
    active->events.insert(active->events.end(),{kind,value});stringEvent(active->separator);stringEvent(active->millis);
    if(active->attempts++==failureIndex)throw RealTextInterrupted();
    if(!active->mutated && mutationMode){active->separator=L"changed";active->millis=std::wstring(L"ms\0",3);active->mutated=true;}
}
class ObservedNumbers:public std::num_put<wchar_t> {
    iter_type do_put(iter_type out,std::ios_base& stream,char_type fill,long value)const override{observe(1,value);return std::num_put<wchar_t>::do_put(out,stream,fill,value);}
    iter_type do_put(iter_type out,std::ios_base& stream,char_type fill,unsigned long value)const override{observe(2,value);return std::num_put<wchar_t>::do_put(out,stream,fill,value);}
};
class GroupedNumbers:public std::numpunct<wchar_t> {
    char_type do_thousands_sep()const override{return wchar_t(0x202f);}
    std::string do_grouping()const override{return "\1";}
};
class ObservedBuffer:public std::wstringbuf {int sync()override{if(active)++active->flushes;return 0;}};
template<class Parser> static Outcome execute(int time,int precision,bool pad,const std::wstring& separator,const std::wstring& millis,int failure,int mutation){
    Parser parser;Outcome result={};result.output=L"sentinel";result.separator=separator;result.millis=millis;active=&result;failureIndex=failure;mutationMode=mutation;
    try{result.output=parser.FormatTimecode(time,precision,pad,result.separator,result.millis);}catch(const RealTextInterrupted&){result.status=2;}catch(const std::ios_base::failure&){result.status=3;}
    active=nullptr;return result;
}
static uint64_t comparisons=0,operations=0,interruptions=0,mutations=0,outputCases=0,flushes=0;
static void compare(int time,int precision=3,bool pad=true,const std::wstring& separator=L":",const std::wstring& millis=L".",int failure=-1,int mutation=0){
    const auto a=execute<ReferenceRealText::CRealTextParser>(time,precision,pad,separator,millis,failure,mutation),b=execute<CandidateRealText::CRealTextParser>(time,precision,pad,separator,millis,failure,mutation);
    if(!(a==b)){fprintf(stderr,"RealText format mismatch case=%llu time=%d precision=%d pad=%d failure=%d mutation=%d\n",(unsigned long long)comparisons,time,precision,pad,failure,mutation);std::abort();}
    ++comparisons;operations+=a.attempts;interruptions+=failure>=0 && a.attempts>failure;mutations+=a.mutated;
}
template<class Parser> static Outcome output(const RealTextLineMap& lines,unsigned flags,int failure){
    Parser parser;parser.m_RealText.m_mapLines=lines;Outcome result={};active=&result;failureIndex=failure;mutationMode=0;ObservedBuffer buffer;std::wostream stream(&buffer);
    if(flags&1)stream<<std::hex;if(flags&2)stream<<std::showpos;stream.width(flags&4?8:0);stream.fill(L'_');if(flags&8)stream.exceptions(std::ios::badbit);
    try{result.status=parser.OutputSRT(stream)?1:0;}catch(const RealTextInterrupted&){result.status=2;}catch(const std::ios_base::failure&){result.status=3;}
    result.output=buffer.str();result.state=int(stream.rdstate());active=nullptr;return result;
}
static void compareOutput(const RealTextLineMap& lines,unsigned flags=0,int failure=-1){
    const auto a=output<ReferenceRealText::CRealTextParser>(lines,flags,failure),b=output<CandidateRealText::CRealTextParser>(lines,flags,failure);
    if(!(a==b)){fprintf(stderr,"RealText OutputSRT mismatch case=%llu flags=%u failure=%d\n",(unsigned long long)outputCases,flags,failure);std::abort();}
    ++outputCases;operations+=a.attempts;flushes+=a.flushes;interruptions+=failure>=0 && a.attempts>failure;
}
int main(){
    const std::locale original=std::locale();
    for(bool grouped:{false,true}){
        std::locale locale(std::locale::classic(),new ObservedNumbers());if(grouped)locale=std::locale(locale,new GroupedNumbers());std::locale::global(locale);
        for(int time:{INT_MIN,INT_MIN+1,-3723999,-1001,-999,-1,0,1,999,1000,1001,3723999,INT_MAX-1,INT_MAX})for(int precision:{-214748361,-1000,-3,-1,0,1,2,3,4,INT_MAX})for(bool pad:{false,true})for(const std::wstring separator:std::initializer_list<std::wstring>{L"",L":",L"sep",std::wstring(L"x\0y",3)})compare(time,precision,pad,separator,separator);
        for(unsigned unit=0;unit<=0xffff;++unit)compare(int(unit)*1001,unit%5,unit%2!=0,std::wstring(1,wchar_t(unit)),std::wstring(1,wchar_t(0xffff-unit)));
        uint32_t random=0x42986217;
        for(unsigned i=0;i<12000;++i){random^=random<<13;random^=random>>17;random^=random<<5;compare(int32_t(random),int(i%30)-20,i%2!=0,L":",L".",-1,i%41==0?1:0);}
        for(int failure=-1;failure<5;++failure)for(int mutation:{0,1})for(int time:{INT_MIN,-999,0,3723999,INT_MAX})compare(time,1,false,L":",L".",failure,mutation);
        const RealTextLineMap lines={{{-3723999,-999},L"first"},{{0,4000},std::wstring(L"raw\0text",8)},{{INT_MAX,INT_MIN},L"reversed"}};
        for(unsigned flags=0;flags<16;++flags)for(int failure=-1;failure<30;++failure)compareOutput(lines,flags,failure);
        for(unsigned i=0;i<1000;++i){RealTextLineMap generated;for(unsigned j=0;j<i%9;++j){random^=random<<13;random^=random>>17;random^=random<<5;generated[{int32_t(random),int32_t(random^0x81253467)}]=std::wstring(1,wchar_t(random&0xffff));}compareOutput(generated,i%16);}
    }
    std::locale::global(std::locale(std::locale::classic(),new ObservedNumbers()));
    if(execute<CandidateRealText::CRealTextParser>(3723999,1,true,L":",L".",-1,0).output!=L"1:2:3.49" || !mutations || !interruptions || !flushes)return 1;
    int32_t untouched[4]={7,8,9,10};if(xy_vsfilter_realtext_format(999,INT_MIN,untouched)!=0 || untouched[0]!=7 || untouched[3]!=10)return 1;
    std::locale::global(original);
    printf("%llu exact RealText timestamp comparisons and %llu complete OutputSRT comparisons passed (%llu native numeric insertions, %llu injected stream failures, %llu live separator mutations, %llu stream flushes; signed INT boundaries, linear precision, ignored padding, NUL separators, native locale grouping and stream flags; unrepresentable precision excluded from parity)\n",(unsigned long long)comparisons,(unsigned long long)outputCases,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)flushes);
}
