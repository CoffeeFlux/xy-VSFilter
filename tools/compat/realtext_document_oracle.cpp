// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
struct Outcome {
    std::vector<int64_t> events,states,conversions;std::vector<int> returns;int attempts;
    bool operator==(const Outcome& o)const{return events==o.events && states==o.states && conversions==o.conversions && returns==o.returns && attempts==o.attempts;}
};
static Outcome* active=nullptr;static int failureIndex=-1;
template<class Parser> struct Context {static Parser* parser;};
template<class Parser> Parser* Context<Parser>::parser=nullptr;
static void stringEvent(std::vector<int64_t>& events,const std::wstring& s){events.push_back(s.size());for(wchar_t c:s)events.push_back(c);}
template<class Tag> static void tagEvent(std::vector<int64_t>& events,const Tag& tag){events.insert(events.end(),{tag.m_bOpen,tag.m_bClose,tag.m_bComment,tag.m_bText});stringEvent(events,tag.m_szName);events.push_back(tag.m_mapAttributes.size());for(const auto& pair:tag.m_mapAttributes){stringEvent(events,pair.first);stringEvent(events,pair.second);}}
template<class Parser> static void stateEvent(std::vector<int64_t>& events,Parser& parser){
    const auto& state=parser.GetParsedSubtitles();events.push_back(state.m_mapLines.size());for(const auto& line:state.m_mapLines){events.insert(events.end(),{line.first.first,line.first.second});stringEvent(events,line.second);}tagEvent(events,state.m_WindowTag);tagEvent(events,state.m_FontTag);events.push_back(state.m_bCenter);
}
template<class Parser> static void observe(int action,int64_t input,int64_t output){
    active->events.insert(active->events.end(),{action,input,output});stateEvent(active->events,*Context<Parser>::parser);
    if(active->attempts++==failureIndex)throw RealTextInterrupted();
}
template<class Parser> static Outcome execute(const std::vector<std::wstring>& docs,unsigned options,int failure){
    Parser parser;parser.m_bTryToIgnoreErrors=(options&1)==0;parser.m_bIgnoreFont=(options&2)!=0;parser.m_bIgnoreFontWeight=(options&4)!=0;parser.m_bIgnoreFontSize=(options&8)!=0;parser.m_bIgnoreFontColor=(options&16)!=0;parser.m_bIgnoreFontFace=(options&32)!=0;
    Outcome result={};active=&result;Context<Parser>::parser=&parser;failureIndex=failure;realTextDocumentHook=&observe<Parser>;realTextConversions.clear();
    for(const auto& doc:docs){int status=0;try{status=parser.ParseRealText(doc)?1:0;}catch(const RealTextInterrupted&){status=2;}result.returns.push_back(status);stateEvent(result.states,parser);}
    result.conversions=realTextConversions;realTextDocumentHook=nullptr;active=nullptr;return result;
}
static uint64_t sequences=0,parses=0,operations=0,interruptions=0,successful=0,failed=0,observations=0;
static void compare(const std::vector<std::wstring>& docs,unsigned options=0,int failure=-1){
    const auto a=execute<ReferenceRealText::CRealTextParser>(docs,options,failure),b=execute<CandidateRealText::CRealTextParser>(docs,options,failure);
    if(!(a==b)){fprintf(stderr,"RealText document mismatch sequence=%llu options=%u failure=%d\n",(unsigned long long)sequences,options,failure);std::abort();}
    ++sequences;parses+=docs.size();operations+=a.attempts;observations+=a.events.size()+a.states.size();for(int status:a.returns){interruptions+=status==2;successful+=status==1;failed+=status==0;}
}
int main(){
    const std::wstring primer=L"<window width='300'><center><time begin='0' end='4'/><font size='20'>first</font></window>";
    for(unsigned unit=0;unit<=0xffff;++unit){const std::wstring raw(1,wchar_t(unit));compare({L"<window raw='"+raw+L"'><time begin='1' end='3'/>"+raw+L"</window>"},unit%64);}
    uint32_t random=0x72985634;const std::wstring fragments[]={L"<window>",L"</window>",L"</center>",L"<time begin='1' end='4'/>",L"<time begin='0'/>",L"<clear/>",L"<b>",L"</b>",L"<font size='20'>",L"</font>",L"<required/>",L"<>",L"<unknown>",L" body ",L"<!comment>",L"<br/>",L"<incomplete",L"\\n",std::wstring(L"raw\0text",8)};
    for(unsigned value=0;value<4000;++value){std::wstring doc;for(unsigned i=0;i<value%21;++i){random^=random<<13;random^=random>>17;random^=random<<5;doc+=fragments[random%19];}compare({primer,doc,L"<time begin='0'/>last<time begin='2'/><clear/>"},value%64);}
    const std::wstring samples[]={L"",L"<",L"<!comment>",L"<window changed='yes'><center/><time begin='1'/>body<time begin='0'/>last<clear/>",L"<time begin='0' end='2'/><b/><i>one<clear/>two</i></window>",L"<time begin='1'/>font<time begin='2'/>text<clear/>",L"<font size='20' color='red'>body<time begin='1'/><clear/>last</font>"};
    for(const auto& doc:samples)for(unsigned options:{0u,1u,4u,63u})for(int failure=-1;failure<100;++failure)compare({primer,doc,primer},options,failure);
    const auto control=execute<CandidateRealText::CRealTextParser>({primer,L"<!comment>"},0,-1);
    if(control.returns!=std::vector<int>({1,0}) || !interruptions || !successful || !failed)return 1;
    printf("%llu exact RealText document sequences / %llu complete parses passed (%llu successful returns, %llu malformed returns, %llu native classification/conversion callbacks, %llu injected interruptions, %llu public-state observation values; every UTF-16 value, tag precedence, final flush, partial state and repeated calls; native arithmetic limited to representable fixtures)\n",(unsigned long long)sequences,(unsigned long long)parses,(unsigned long long)successful,(unsigned long long)failed,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)observations);
}
