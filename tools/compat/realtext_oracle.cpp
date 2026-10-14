// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_realtext.h"
struct TagInput {std::wstring name;unsigned flags;std::map<std::wstring,std::wstring> attributes;};
struct Outcome {
    std::wstring text;std::vector<int64_t> conversions;
    bool operator==(const Outcome& o)const{return text==o.text && conversions==o.conversions;}
};
static uint64_t comparisons=0,nonempty=0,units=0,conversions=0,parses=0;
template<class Parser> static Outcome execute(const std::vector<TagInput>& input,unsigned ignore,int minimum,int maximum){
    Parser parser;parser.m_bIgnoreFont=ignore&1;parser.m_bIgnoreFontSize=ignore&2;parser.m_bIgnoreFontColor=ignore&4;parser.m_bIgnoreFontWeight=ignore&8;parser.m_bIgnoreFontFace=ignore&16;parser.m_iMinFontSize=minimum;parser.m_iMaxFontSize=maximum;
    std::list<typename Parser::Tag> tags;
    for(const auto& source:input){typename Parser::Tag tag;tag.m_szName=source.name;tag.m_bOpen=source.flags&1;tag.m_bClose=source.flags&2;tag.m_bText=source.flags&4;tag.m_bComment=source.flags&8;tag.m_mapAttributes=source.attributes;tags.push_back(tag);}
    realTextConversions.clear();Outcome result;result.text=parser.RenderTags(tags);result.conversions=realTextConversions;return result;
}
static void compare(const std::vector<TagInput>& input,unsigned ignore=0,int minimum=14,int maximum=25){
    auto a=execute<ReferenceRealText::CRealTextParser>(input,ignore,minimum,maximum);auto b=execute<CandidateRealText::CRealTextParser>(input,ignore,minimum,maximum);
    if(!(a==b)){fprintf(stderr,"RealText tag mismatch case=%llu count=%zu ignore=%u min=%d max=%d\n",(unsigned long long)comparisons,input.size(),ignore,minimum,maximum);std::abort();}
    ++comparisons;nonempty+=!a.text.empty();units+=a.text.size();conversions+=a.conversions.size();
}
template<class A,class B> static bool sameTag(const A& a,const B& b){
    return a.m_szName==b.m_szName && a.m_bOpen==b.m_bOpen && a.m_bClose==b.m_bClose && a.m_bComment==b.m_bComment && a.m_bText==b.m_bText && a.m_mapAttributes==b.m_mapAttributes;
}
static void parseSequence(const std::vector<std::wstring>& texts,unsigned options=0){
    ReferenceRealText::CRealTextParser a;CandidateRealText::CRealTextParser b;
    a.m_bTryToIgnoreErrors=b.m_bTryToIgnoreErrors=(options&1)==0;
    a.m_bIgnoreFont=b.m_bIgnoreFont=(options&2)!=0;
    a.m_bIgnoreFontWeight=b.m_bIgnoreFontWeight=(options&4)!=0;
    a.m_bIgnoreFontSize=b.m_bIgnoreFontSize=(options&8)!=0;
    a.m_bIgnoreFontColor=b.m_bIgnoreFontColor=(options&16)!=0;
    a.m_bIgnoreFontFace=b.m_bIgnoreFontFace=(options&32)!=0;
    a.m_iDefaultSubtitleDurationInMillisecs=b.m_iDefaultSubtitleDurationInMillisecs=options&64?0:4000;
    for(const auto& text:texts){
        realTextConversions.clear();const bool ar=a.ParseRealText(text);const auto ac=realTextConversions;
        realTextConversions.clear();const bool br=b.ParseRealText(text);const auto bc=realTextConversions;
        const auto& as=a.GetParsedSubtitles();const auto& bs=b.GetParsedSubtitles();
        if(ar!=br || ac!=bc || as.m_mapLines!=bs.m_mapLines || as.m_bCenter!=bs.m_bCenter || !sameTag(as.m_WindowTag,bs.m_WindowTag) || !sameTag(as.m_FontTag,bs.m_FontTag)){fprintf(stderr,"RealText full parse mismatch case=%llu options=%u\n",(unsigned long long)parses,options);std::abort();}++parses;
    }
}
static void parse(const std::wstring& text){parseSequence({text});}
int main(){
    const std::wstring names[]={L"br",L"b",L"i",L"font",L"u",L"B",L"Font",L"",L"text",L" ",std::wstring(L"font\0tail",9)};
    const std::wstring sizes[]={L"",L"0",L"1",L"13",L"14",L"25",L"26",L"2147483647",L"-1",L"+20",L" 20",L"20px",std::wstring(L"20\0x",4),L"\u0662\u0660"};
    const TagInput text={L"text",4,{}};
    for(const auto& name:names)for(unsigned flags=0;flags<16;++flags)for(unsigned ignore=0;ignore<32;++ignore)for(int variant=0;variant<4;++variant){
        TagInput tag={name,flags,{{L"color",L"red"},{L"face",L"Arial"},{L"size",sizes[(flags+ignore)%14]},{L"unknown",L"\"<&"}}};
        std::vector<TagInput> input={tag};if(variant&1)input.push_back(text);
        compare(input,ignore,variant&2?30:14,variant&2?-1:25);
    }
    for(unsigned value=0;value<=0xffff;++value){std::wstring raw(1,wchar_t(value));compare({{L"font",3,{{L"size",raw},{L"raw",raw}}},{raw,4,{}}});}
    uint32_t random=0x13465197;
    for(unsigned value=0;value<12000;++value){std::vector<TagInput> input;
        for(unsigned i=0;i<value%17;++i){random^=random<<13;random^=random>>17;random^=random<<5;input.push_back({names[random%11],(random>>8)%16,{{L"color",L"red"},{L"face",L"Arial"},{L"size",sizes[(random>>16)%14]}}});}
        compare(input,value%32,int(value%40)-10,int(value%31)-5);
    }
    for(const auto& size:sizes)for(const auto& name:{L"b",L"i",L"u",L"font"}){
        // Numeric parsing and the surrounding native parser are retained. These
        // complete documents establish composition with the new output owner.
        parse(L"<window><time begin=\"0\" end=\"4\"/><"+std::wstring(name)+L" size=\""+size+L"\">first</"+name+L"><br/>second<time begin=\"4\" end=\"8\"/><clear/><i>next</i></window>");
    }
    parse(L"<window><time begin=\"0\" end=\"4\"/><i></i></window>");
    parse(L"<window><time begin=\"0\"/><i>first<time begin=\"4\"/><clear/>second</i></window>");
    const std::wstring fragments[]={L"<window width='300'>",L"</window>",L"<center/>",L"<time begin='1' end='4'/>",L"<time begin='3'/>",L"<time begin='0' end='2'/>",L"<clear/>",L"<i>",L"</i>",L"<b/>",L"<font size='20' color='red'>",L"</font>",L"<!comment>",L"<required>",L"<unknown>",L" body ",L"\n\t",L"<br/>",L"<font a='one\" b='two'>",L"<incomplete",std::wstring(L"raw\0text",8)};
    const std::wstring primer=L"<window><center><time begin='0'/>first<time begin='3'/><clear/><font face='Arial'>second</font></window>";
    for(unsigned value=0;value<10000;++value){
        std::wstring middle;for(unsigned i=0;i<value%31;++i){random^=random<<13;random^=random>>17;random^=random<<5;middle+=fragments[random%21];}
        // Reuse each parser across success and failure: its stored subtitles,
        // window and center state intentionally survive subsequent parses.
        parseSequence({primer,middle,value%2?L"<time begin='0' end='2'/>last<clear/>":L"<window changed='yes'><time begin='1'/>unfinished"},value%128);
    }
    const auto control=execute<CandidateRealText::CRealTextParser>({{L"i",3,{}},text},0,14,25);
    if(control.text!=L"<i>text" || !nonempty || !conversions){fprintf(stderr,"RealText output controls failed\n");return 1;}
    printf("%llu exact RealText tag-output comparisons and %llu complete native parses passed (%llu nonempty outputs, %llu compared UTF-16 units, %llu native classification/conversion observation values; all ignore flags, open/close/text precedence, ordered raw attributes, font-size boundaries, every UTF-16 code unit and formatting-only suppression)\n",(unsigned long long)comparisons,(unsigned long long)parses,(unsigned long long)nonempty,(unsigned long long)units,(unsigned long long)conversions);
}
