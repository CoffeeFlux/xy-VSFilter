// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ass_document.h"
static uint64_t comparisons=0,accepted=0,rejected=0,escaped=0,callbacks=0,styles=0,dialogues=0,interruptions=0;
static void compare(const std::vector<DocumentLine>& lines,bool unicode=true,int failure=-1,bool mutate=false){
    std::wstring diagnostic;for(const auto& line:lines){diagnostic+=line.text;diagnostic+=L'\n';}
    microCase=comparisons;microInput=diagnostic.data();microLength=static_cast<int>(diagnostic.size());microSide=0;
    DocumentFile first(lines,unicode),second(lines,unicode);DocumentSubtitle a,b;
    a.m_dstScreenSize=DocumentSize(-1,0);b.m_dstScreenSize=a.m_dstScreenSize;
    a.m_layout_size=DocumentSize(640,360);b.m_layout_size=a.m_layout_size;
    bool resultA=false,resultB=false;int exceptionA=0,exceptionB=0;
    documentMutate=mutate;activeDocument=&a;searchFailure=failure;searchAttempts=0;searchEvents.clear();errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);
    try{resultA=ReferenceAssDocument::OpenSubStationAlpha(&first,a,1);}catch(const SearchInterrupted&){exceptionA=1;}catch(int value){exceptionA=2+value;}
    const int errnoA=errno,fpA=std::fetestexcept(FE_ALL_EXCEPT);const auto events=searchEvents;const int attempts=searchAttempts;
    if(liveStyles)std::abort();
    activeDocument=&b;searchAttempts=0;searchEvents.clear();errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);microSide=1;
    try{resultB=CandidateAssDocument::OpenSubStationAlpha(&second,b,1);}catch(const SearchInterrupted&){exceptionB=1;}catch(int value){exceptionB=2+value;}
    const int errnoB=errno,fpB=std::fetestexcept(FE_ALL_EXCEPT);
    if(liveStyles || resultA!=resultB || exceptionA!=exceptionB || a.settings()!=b.settings() || a.StyleSubtitle::entries!=b.StyleSubtitle::entries || a.DialogueSubtitle::entries!=b.DialogueSubtitle::entries || first.position!=second.position || events!=searchEvents || errnoA!=errnoB || fpA!=fpB){
        fprintf(stderr,"ASS document mismatch failure=%d mutate=%d accepted=%d/%d exceptions=%d/%d read=%zu/%zu callbacks=%zu/%zu errno=%d/%d fp=%d/%d\n",failure,mutate,resultA,resultB,exceptionA,exceptionB,first.position,second.position,events.size(),searchEvents.size(),errnoA,errnoB,fpA,fpB);std::abort();
    }
    activeDocument=NULL;++comparisons;accepted+=resultA;rejected+=!resultA && !exceptionA;escaped+=exceptionA!=0;callbacks+=events.size();styles+=a.StyleSubtitle::entries.size();dialogues+=a.DialogueSubtitle::entries.size();interruptions+=failure>=0 && attempts>failure;
}
static std::vector<DocumentLine> script(const std::vector<std::wstring>& lines,bool finalSuccess=true){
    std::vector<DocumentLine> result;for(const auto& line:lines)result.push_back({line,true});
    if(!result.empty())result.back().success=finalSuccess;return result;
}
int main(){
    microDiagnostics();microObserveFieldTrims=false;
    const std::wstring style=L"Style: Default,Arial,36,16777215,255,0,0,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1";
    const std::wstring dialogue=L"Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,visible";
    const wchar_t* headers[]={L"",L" ",L";comment",L"[Script Info]",L"[Events]",L"ScriptType: v4.00+",L"ScriptType: v4.00",L"ScriptType: v4.00++",L"ScriptType: prefix4.00+",L"ScriptType: v4.00+ ",L"[V4 Styles]",L"[V4+ Styles]",L"[V4++ Styles]",L"PlayResX: 1280",L"PlayResY: 1024",L"PlayResX: 640",L"PlayResY: 360",L"PlayResX: -1",L"PlayResY: 0",L"LayoutResX: 800",L"LayoutResY: 600",L"WrapStyle: -1",L"Collisions: unreversed",L"Collisions: normal",L"ScaledBorderAndShadow: notyes",L"ScaledBorderAndShadow: no",L"YCbCr Matrix: none trailing",L"YCbCr Matrix: tv.601x",L"YCbCr Matrix: tv.709",L"YCbCr Matrix: tv.2020",L"YCbCr Matrix: pc.601",L"YCbCr Matrix: pc.709",L"YCbCr Matrix: pc.2020",L"YCbCr Matrix: unknown",L"WrapStyle: junk",L"PlayResX: junk",L"LayoutResY: junk",L"unknown: anything"};
    for(const auto* a:headers)for(const auto* b:headers){
        auto lines=script({a,b,L"ScriptType: v4.00+",L"[V4+ Styles]",style,dialogue});compare(lines);
        compare(script({L"[Script Info]",a,b},false),false,-1,true);
    }
    for(const auto* header:headers){
        const std::wstring text=header;
        for(size_t i=0;i<=text.size();++i){auto nul=text;nul.insert(i,1,wchar_t(0));compare(script({L"[Script Info]",nul,dialogue}));}
    }
    const std::wstring legacyStyle=L"Style: Default,Arial,36,16777215,255,0,0,0,0,1,2,1,2,10,10,10,0,1";
    compare(script({L"ScriptType: v4.00+",L"[V4 Styles]",L"ScriptType: unknown",legacyStyle,L"Dialogue: 7,0:00:01.00,0:00:04.00,Default,,0,0,0,,visible"}));
    compare(script({L"ScriptType: v4.00+",style,dialogue}));
    for(unsigned unit=0;unit<=0xffff;++unit){
        std::wstring text=L"YCbCr Matrix: ";text.push_back(static_cast<wchar_t>(unit));text+=L"pc.709";
        compare(script({L"[Script Info]",text,L"WrapStyle: 1"},unit%2==0));
        std::wstring entry=L"[Events]";entry.push_back(static_cast<wchar_t>(unit));
        compare(script({entry},false));
    }
    const auto font=script({L"[Script Info]",L"ScriptType: v4.00+",L"fontname: sample.ttf",L"abcd",L"fontname: next.ttf",L"efgh",L"[Events]",dialogue});
    for(int failure=-1;failure<90;++failure)compare(font,true,failure);
    const auto mixed=script({L"[Script Info]",L"PlayResX: 1280",L"PlayResY: 720",L"ScriptType: v4.00+",L"[V4+ Styles]",style,L"YCbCr Matrix: pc.709",dialogue});
    for(int failure=-1;failure<110;++failure)compare(mixed,true,failure,true);
    uint32_t random=0x906381c7;
    for(unsigned n=0;n<12000;++n){std::vector<std::wstring> input;
        for(unsigned i=0;i<n%17;++i){random^=random<<13;random^=random>>17;random^=random<<5;input.push_back(headers[random%(sizeof(headers)/sizeof(*headers))]);}
        input.push_back(style);input.push_back(dialogue);compare(script(input,n%3!=0),n%2==0,-1,n%5==0);
    }
    if(!accepted || !rejected || !escaped || !styles || !dialogues || !interruptions || !documentFonts || !documentMutations || !styleOwners || !dialogueOwners || !headerOwners || !headerClassifications || !headerValues || !headerVersions)return 1;
    printf("%llu exact ASS document comparisons passed (%llu accepted, %llu rejected, %llu escaping exceptions, %llu style calls, %llu dialogue calls, %llu callback observation values, %llu injected interruptions, %llu read callbacks, %llu font callbacks, %llu read-time setting mutations; %llu header owners / %llu classifications / %llu value selections / %llu version replies; complete pinned OpenSubStationAlpha and LoadUUEFont with migrated style/dialogue branches; final-line EOF, repeated headers, charset versions, partial state and file position; line I/O/FastTrim/style-map/font-registration boundaries modeled, unrepresentable numeric arithmetic excluded)\n",(unsigned long long)comparisons,(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)escaped,(unsigned long long)styles,(unsigned long long)dialogues,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)documentReads,(unsigned long long)documentFonts,(unsigned long long)documentMutations,(unsigned long long)headerOwners,(unsigned long long)headerClassifications,(unsigned long long)headerValues,(unsigned long long)headerVersions);
}
