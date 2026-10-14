// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
struct Outcome {
    std::vector<int64_t> events,states,conversions;std::vector<int> returns;int attempts,duration;bool mutated;
    bool operator==(const Outcome& o)const{return events==o.events && states==o.states && conversions==o.conversions && returns==o.returns && attempts==o.attempts && duration==o.duration && mutated==o.mutated;}
};
static Outcome* active=nullptr;static int* activeDuration=nullptr;static int failureIndex=-1,mutationMode=0;
static void stringEvent(std::vector<int64_t>& events,const std::wstring& s){events.push_back(s.size());for(wchar_t c:s)events.push_back(c);}
static void mapEvent(std::vector<int64_t>& events,const RealTextLineMap& lines){events.push_back(lines.size());for(const auto& pair:lines){events.insert(events.end(),{pair.first.first,pair.first.second});stringEvent(events,pair.second);}}
template<class Tag> static void tagEvent(std::vector<int64_t>& events,const Tag& tag){
    events.insert(events.end(),{tag.m_bOpen,tag.m_bClose,tag.m_bComment,tag.m_bText});stringEvent(events,tag.m_szName);events.push_back(tag.m_mapAttributes.size());for(const auto& pair:tag.m_mapAttributes){stringEvent(events,pair.first);stringEvent(events,pair.second);}
}
static void observe(RealTextLineMap& lines,const std::pair<int,int>& key,const std::wstring& value){
    active->events.insert(active->events.end(),{key.first,key.second,*activeDuration});stringEvent(active->events,value);mapEvent(active->events,lines);
    if(active->attempts++==failureIndex)throw RealTextInterrupted();
    if(!active->mutated && mutationMode){
        if(mutationMode==1)*activeDuration=-10;
        if(mutationMode==2)lines.clear();
        if(mutationMode==3)lines[std::make_pair(-10,10)]=L"callback";
        active->mutated=true;
    }
}
template<class Parser> static Outcome execute(const std::vector<std::wstring>& docs,int duration,bool ignore,int failure,int mutation){
    Parser parser;parser.m_iDefaultSubtitleDurationInMillisecs=duration;parser.m_bTryToIgnoreErrors=ignore;
    Outcome result={};active=&result;activeDuration=&parser.m_iDefaultSubtitleDurationInMillisecs;failureIndex=failure;mutationMode=mutation;realTextLineHook=observe;realTextConversions.clear();
    for(const auto& doc:docs){
        int status=0;try{status=parser.ParseRealText(doc)?1:0;}catch(const RealTextInterrupted&){status=2;}
        result.returns.push_back(status);const auto& state=parser.GetParsedSubtitles();mapEvent(result.states,state.m_mapLines);tagEvent(result.states,state.m_WindowTag);tagEvent(result.states,state.m_FontTag);result.states.push_back(state.m_bCenter);
    }
    result.duration=parser.m_iDefaultSubtitleDurationInMillisecs;result.conversions=realTextConversions;realTextLineHook=nullptr;active=nullptr;activeDuration=nullptr;return result;
}
static uint64_t comparisons=0,parses=0,operations=0,interruptions=0,mutations=0,successful=0,failed=0;
static void compare(const std::vector<std::wstring>& docs,int duration=4000,bool ignore=true,int failure=-1,int mutation=0){
    const auto a=execute<ReferenceRealText::CRealTextParser>(docs,duration,ignore,failure,mutation),b=execute<CandidateRealText::CRealTextParser>(docs,duration,ignore,failure,mutation);
    if(!(a==b)){fprintf(stderr,"RealText timing mismatch sequence=%llu duration=%d ignore=%d failure=%d mutation=%d\n",(unsigned long long)comparisons,duration,ignore,failure,mutation);std::abort();}
    ++comparisons;parses+=docs.size();operations+=a.attempts;mutations+=a.mutated;for(int status:a.returns){interruptions+=status==2;successful+=status==1;failed+=status==0;}
}
static std::wstring event(int start,int end){std::wostringstream s;s<<L"<time begin='"<<start<<L"' end='"<<end<<L"'/>";return s.str();}
int main(){
    const std::wstring primer=L"<window><center><time begin='0' end='4'/>old</window>";
    for(int start:{-1,0,1,2,4})for(int end:{-1,0,1,2,4})for(int next:{-1,0,1,2,4})for(int last:{-1,0,1,2,4})for(int duration:{-4000,0,1,4000})for(unsigned mode=0;mode<4;++mode){
        std::wstring doc=L"<window>";if(mode&1)doc+=L"lead";doc+=event(start,end)+L"first";doc+=event(next,last);if(mode&2)doc+=L"<clear/>";doc+=L"second</window>";
        compare({mode&1?primer:L"",doc,L"<time begin='2'/>tail<clear/>"},duration);
    }
    uint32_t random=0x71154265;const std::wstring fragments[]={L"<window>",L"</window>",L"<center/>",event(0,0),event(1,4),event(2,-1),event(0,2),L"<clear/>",L"<i>",L"</i>",L" body ",L"<!comment>",L"<br/>",L"<incomplete",L"<time begin='1:2'/>",L"<time end='3'/>"};
    for(unsigned value=0;value<12000;++value){std::wstring doc;for(unsigned i=0;i<value%41;++i){random^=random<<13;random^=random>>17;random^=random<<5;doc+=fragments[random%16];}compare({primer,doc,L"<time begin='0'/>last<time begin='2'/><clear/>"},int(value%8001)-4000,value%2!=0,-1,value%7==0?int(value%4):0);}
    const std::wstring samples[]={L"lead<time begin='1'/>first<time begin='0'/><clear/>next<time begin='0'/>last<clear/>",L"<time begin='1' end='4'/>first<time begin='3' end='2'/>second<clear/>",L"<time begin='2'/>body</window>"};
    for(const auto& doc:samples)for(int duration:{-4000,0,1,4000})for(int mutation=0;mutation<4;++mutation)for(int failure=-1;failure<8;++failure)compare({primer,doc,doc},duration,true,failure,mutation);
    // These values and zero duration keep every native multiply/add representable.
    compare({L"<time begin='2147483.647'/>x<time begin='2147483.646'/>y</window>"},0);
    const auto control=execute<CandidateRealText::CRealTextParser>({L"lead<time begin='1'/>first<time begin='0'/><clear/>next</window>"},4000,true,-1,0);
    if(!control.returns[0] || control.attempts!=3 || !interruptions || !mutations || !successful || !failed)return 1;
    printf("%llu exact RealText timing sequences / %llu complete parses passed (%llu successful returns, %llu malformed returns, %llu native map insertion attempts, %llu injected interruptions, %llu live duration/map mutations; missing ends, first-line rules, duplicate/reversed intervals, final flush and state across parses; native arithmetic restricted to representable fixtures)\n",(unsigned long long)comparisons,(unsigned long long)parses,(unsigned long long)successful,(unsigned long long)failed,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)mutations);
}
