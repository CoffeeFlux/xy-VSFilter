// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
struct Input {std::wstring name;unsigned flags,action;std::map<std::wstring,std::wstring> attributes;};
struct Outcome {std::vector<int64_t> states,conversions;std::vector<std::wstring> rendered;bool operator==(const Outcome& o)const{return states==o.states && conversions==o.conversions && rendered==o.rendered;}};
static uint64_t comparisons=0,operations=0,observations=0,rendered=0;
static void stringEvent(std::vector<int64_t>& events,const std::wstring& s){events.push_back(s.size());for(wchar_t c:s)events.push_back(c);}
template<class Tag> static void listEvent(std::vector<int64_t>& events,const std::list<Tag>& tags){
    events.push_back(tags.size());for(const auto& tag:tags){events.insert(events.end(),{tag.m_bOpen,tag.m_bClose,tag.m_bComment,tag.m_bText});stringEvent(events,tag.m_szName);events.push_back(tag.m_mapAttributes.size());for(const auto& pair:tag.m_mapAttributes){stringEvent(events,pair.first);stringEvent(events,pair.second);}}
}
template<class Tag> static Tag makeTag(const Input& source){Tag tag;tag.m_szName=source.name;tag.m_bOpen=source.flags&1;tag.m_bClose=source.flags&2;tag.m_bComment=source.flags&4;tag.m_bText=source.flags&8;tag.m_mapAttributes=source.attributes;return tag;}
static Outcome reference(const std::vector<Input>& inputs){
    ReferenceRealText::CRealTextParser parser;std::list<ReferenceRealText::CRealTextParser::Tag> tags,open;Outcome result;realTextConversions.clear();
    for(const auto& input:inputs){
        auto tag=makeTag<ReferenceRealText::CRealTextParser::Tag>(input);
        // These are the native list operations used by ParseRealText. Closing
        // delegates to the actual pinned PopTag method, not a rewritten search.
        if(input.action==2){tags.clear();tags.insert(tags.end(),open.begin(),open.end());}
        else {if(input.action==1){if(tag.m_bOpen)open.push_back(tag);if(tag.m_bClose)parser.PopTag(open,tag.m_szName);}tags.push_back(tag);}
        tag.m_szName=L"changed";tag.m_mapAttributes.clear();
        listEvent(result.states,tags);listEvent(result.states,open);result.rendered.push_back(parser.RenderTags(tags));
    }
    result.conversions=realTextConversions;return result;
}
static Outcome candidate(const std::vector<Input>& inputs){
    CandidateRealText::CRealTextParser parser;XyRust::RealTextLists lists;Outcome result;realTextConversions.clear();
    for(const auto& input:inputs){
        auto tag=makeTag<CandidateRealText::CRealTextParser::Tag>(input);
        if(input.action==2)lists.Clear();else{lists.Set(tag);if(input.action==1){if(tag.m_bOpen)lists.Open();if(tag.m_bClose)lists.Close();}lists.Append();}
        tag.m_szName=L"changed";tag.m_mapAttributes.clear();
        const auto tags=lists.Export<CandidateRealText::CRealTextParser::Tag>(),open=lists.Export<CandidateRealText::CRealTextParser::Tag>(1);
        listEvent(result.states,tags);listEvent(result.states,open);result.rendered.push_back(parser.RenderTags(tags));
    }
    result.conversions=realTextConversions;return result;
}
static void compare(const std::vector<Input>& input){
    const auto a=reference(input),b=candidate(input);
    if(!(a==b)){fprintf(stderr,"RealText tag-list mismatch sequence=%llu operations=%zu\n",(unsigned long long)comparisons,input.size());std::abort();}
    ++comparisons;operations+=input.size();observations+=a.states.size();for(const auto& line:a.rendered)rendered+=!line.empty();
}
int main(){
    const std::wstring names[]={L"b",L"i",L"font",L"br",L"",L"B",L"text",std::wstring(L"i\0tail",6)};
    for(const auto& name:names)for(unsigned flags=0;flags<16;++flags)for(unsigned action=0;action<3;++action){
        compare({{name,1,1,{{L"a",L"first"}}},{L"i",1,1,{}},{name,1,1,{{L"a",L"second"}}},{name,flags,action,{{L"size",L"20"}}},{L"",0,2,{}},{L"text",8,0,{}}});
    }
    for(unsigned unit=0;unit<=0xffff;++unit){const std::wstring raw(1,wchar_t(unit));compare({{raw,1,1,{{raw,raw},{L"size",L"20"}}},{L"other",1,1,{}},{raw,1,1,{{L"a",L"new"}}},{raw,2,1,{}},{L"",0,2,{}},{raw,8,0,{}}});}
    uint32_t random=0x98271164;
    for(unsigned value=0;value<12000;++value){std::vector<Input> inputs;for(unsigned i=0;i<value%31;++i){random^=random<<13;random^=random>>17;random^=random<<5;inputs.push_back({names[random%8],(random>>8)%16,(random>>16)%3,{{L"color",L"red"},{L"size",L"14"},{std::wstring(1,wchar_t(random&0xffff)),std::wstring(1,wchar_t((random>>16)&0xffff))}}});}compare(inputs);}
    const auto control=candidate({{L"i",1,1,{}},{L"i",1,1,{}},{L"i",2,1,{}},{L"",0,2,{}},{L"body",8,0,{}}});
    if(control.rendered.back()!=L"<i>body" || !rendered)return 1;
    printf("%llu exact RealText tag-list sequences / %llu list operations passed (%llu serialized field observations, %llu nonempty rendered snapshots; reverse matching close, repeated names, open-and-close flags, ordered clear carry, copied native input, maps, NULs and every UTF-16 value; native container allocation failure timing unverified)\n",(unsigned long long)comparisons,(unsigned long long)operations,(unsigned long long)observations,(unsigned long long)rendered);
}
