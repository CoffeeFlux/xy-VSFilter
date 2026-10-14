// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
#include <stdexcept>
struct Outcome {
    std::wstring input,name;std::map<std::wstring,std::wstring> attributes;std::vector<int64_t> events,conversions;
    unsigned flags;int attempts;bool returned,interrupted,range,mutated;
    bool operator==(const Outcome& o)const{return input==o.input && name==o.name && attributes==o.attributes && events==o.events && conversions==o.conversions && flags==o.flags && attempts==o.attempts && returned==o.returned && interrupted==o.interrupted && range==o.range && mutated==o.mutated;}
};
static Outcome* active=nullptr;
static int failureIndex=-1,mutationMode=0;
static void stringEvent(const std::wstring& s){active->events.push_back(s.size());for(wchar_t c:s)active->events.push_back(c);}
template<class Parser> struct Context {static typename Parser::Tag* tag;};
template<class Parser> typename Parser::Tag* Context<Parser>::tag=nullptr;
template<class Tag> static unsigned flags(const Tag& tag){return unsigned(tag.m_bComment) | (unsigned(tag.m_bOpen)<<1) | (unsigned(tag.m_bClose)<<2) | (unsigned(tag.m_bText)<<3);}
template<class Parser> static void observe(int action,const std::wstring& name){
    auto& tag=*Context<Parser>::tag;
    active->events.insert(active->events.end(),{action,flags(tag)});stringEvent(name);stringEvent(active->input);stringEvent(tag.m_szName);
    active->events.push_back(tag.m_mapAttributes.size());for(const auto& pair:tag.m_mapAttributes){stringEvent(pair.first);stringEvent(pair.second);}
    if(active->attempts++==failureIndex)throw RealTextInterrupted();
    if(!active->mutated && mutationMode){
        if(mutationMode==1 && action==1){active->input.clear();active->mutated=true;}
        if(mutationMode==2 && action==2){active->input+=L" extra='changed'/>tail";active->mutated=true;}
        if(mutationMode==3 && action==1){tag.m_bOpen=!tag.m_bOpen;tag.m_bClose=!tag.m_bClose;tag.m_bText=!tag.m_bText;tag.m_bComment=!tag.m_bComment;tag.m_szName+=L"callback";active->mutated=true;}
    }
}
template<class Parser> static Outcome execute(const std::wstring& input,bool ignore,unsigned initial,int failure,int mutation){
    Parser parser;parser.m_bTryToIgnoreErrors=ignore;typename Parser::Tag tag;
    tag.m_bComment=initial&1;tag.m_bOpen=initial&2;tag.m_bClose=initial&4;tag.m_bText=initial&8;
    tag.m_szName=initial&16?L"PRIOR":L"";tag.m_mapAttributes={{L"prior",L"old"},{L"a",L"original"}};
    Outcome result={};result.input=input;active=&result;Context<Parser>::tag=&tag;failureIndex=failure;mutationMode=mutation;realTextTagHook=&observe<Parser>;realTextConversions.clear();
    try{result.returned=parser.ExtractTag(result.input,tag);}catch(const RealTextInterrupted&){result.interrupted=true;}catch(const std::out_of_range&){result.range=true;}
    result.name=tag.m_szName;result.attributes=tag.m_mapAttributes;result.flags=flags(tag);result.conversions=realTextConversions;realTextTagHook=nullptr;active=nullptr;return result;
}
static uint64_t comparisons=0,successful=0,interruptions=0,ranges=0,mutations=0,operations=0,consumed=0;
static void compare(const std::wstring& input,bool ignore=true,unsigned initial=0,int failure=-1,int mutation=0){
    const auto a=execute<ReferenceRealText::CRealTextParser>(input,ignore,initial,failure,mutation);const auto b=execute<CandidateRealText::CRealTextParser>(input,ignore,initial,failure,mutation);
    if(!(a==b)){fprintf(stderr,"RealText extraction mismatch case=%llu length=%zu ignore=%d initial=%u failure=%d mutation=%d flags=%u/%u name=%zu/%zu attempts=%d/%d range=%d/%d\n",(unsigned long long)comparisons,input.size(),ignore,initial,failure,mutation,a.flags,b.flags,a.name.size(),b.name.size(),a.attempts,b.attempts,a.range,b.range);for(wchar_t c:input)fprintf(stderr,"%04x ",unsigned(c));fprintf(stderr,"\n");std::abort();}
    ++comparisons;successful+=a.returned;interruptions+=a.interrupted;ranges+=a.range;mutations+=a.mutated;operations+=a.attempts;consumed+=a.input!=input;
}
int main(){
    const wchar_t units[]={L'a',L'<',L'>',L'!',L'/',L' ',L'=',0};unsigned combinations=1;
    for(unsigned length=0;length<=5;++length){if(length)combinations*=8;for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;for(unsigned i=0;i<length;++i){input.push_back(units[code%8]);code/=8;}for(bool ignore:{false,true})for(unsigned initial:{0u,31u})compare(input,ignore,initial);}}
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring raw(1,wchar_t(unit));compare(L"<"+raw+L" a='one'/>tail",true,unit%32);compare(L"<!"+raw+L">tail",true,unit%32);compare(L"<B a='"+raw+L"'>tail",false,unit%32);}
    uint32_t random=0x46932485;const std::wstring parts[]={L"<",L">",L"!",L"/",L"FONT",L"a=",L"'",L"\"",L"\n",L"\r",L"\t",L" ",L"/>",L"abc",std::wstring(1,0)};
    for(unsigned value=0;value<20000;++value){std::wstring input;for(unsigned i=0;i<value%21;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=parts[random%15];}compare(input,value%2!=0,random%32,-1,value%13==0?int(value%4):0);}
    const std::wstring samples[]={L"",L"<",L"abc",L"<!comment>",L"<!comment>tail",L"< B a='one' A=two/>tail",L"</B>",L"<font x y=one z='two\" q=unfinished",L"prefix< a='\n\t text' />tail",L">name junk=one</x"};
    for(const auto& input:samples)for(bool ignore:{false,true})for(unsigned initial:{0u,15u,16u,31u})for(int mutation=0;mutation<4;++mutation)for(int failure=-1;failure<24;++failure)compare(input,ignore,initial,failure,mutation);
    const auto eof=execute<CandidateRealText::CRealTextParser>(L"<!note>",true,31,-1,0);
    const auto tail=execute<CandidateRealText::CRealTextParser>(L"<!note>x",true,31,-1,0);
    if(eof.returned || eof.name!=L"!note" || eof.input!=L"<!note>" || !tail.returned || tail.input!=L"x" || !ranges || !interruptions || !mutations || !successful)return 1;
    printf("%llu exact RealText tag extraction comparisons passed (%llu successful returns, %llu changed inputs, %llu native whitespace/lowercase operations, %llu injected interruptions, %llu matching checked-string exceptions, %llu callback mutations; comments at EOF, prior names/maps/flags, malformed recovery, partial updates, NULs and every UTF-16 value)\n",(unsigned long long)comparisons,(unsigned long long)successful,(unsigned long long)consumed,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)ranges,(unsigned long long)mutations);
}
