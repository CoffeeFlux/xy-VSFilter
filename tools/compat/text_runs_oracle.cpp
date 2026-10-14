// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_text_runs.h"
static uint64_t comparisons=0,words=0,callbacks=0,interruptions=0,mutations=0;
static RenderState* activeRunState=NULL;
static int runMutation=0;
static unsigned runMutationCount=0;
static void runHook(unsigned kind){
    if((runMutation==1 && kind==59) || (runMutation==2 && kind==60) || (runMutation==3 && (kind==59 || kind==60))){
        activeRunState->m_kend+=3;activeRunState->m_kstart=-7;activeRunState->m_ktype=(activeRunState->m_ktype+1)%8;
        activeRunState->m_target_scale_x=-activeRunState->m_target_scale_x;activeRunState->m_target_scale_y+=0.25;++runMutationCount;
    }
}
static std::vector<uint64_t> state(const RenderState& s){return runRecord(MicroString(),FwSTSStyle{0},s.m_ktype,s.m_kstart,s.m_kend,s.m_target_scale_x,s.m_target_scale_y);}
static std::vector<uint16_t> runUnits(const MicroString& s){std::vector<uint16_t> out;for(int i=0;i<s.GetLength();++i)out.push_back(static_cast<uint16_t>(s[i]));return out;}
static void compare(const std::wstring& input,int wrap,int failure=-1,int mutation=0,bool nullSub=false){
    if(liveRunAllocations){fprintf(stderr,"Word lifetime control failed\n");std::abort();}
    microCase=comparisons;microSide=0;microInput=input.data();microLength=static_cast<int>(input.size());
    ReferenceTextRuns::CRenderedTextSubtitle a;CandidateTextRuns::CRenderedTextSubtitle b;
    a.m_ktype=b.m_ktype=1;a.m_kstart=b.m_kstart=-5;a.m_kend=b.m_kend=13;a.m_target_scale_x=b.m_target_scale_x=1.25;a.m_target_scale_y=b.m_target_scale_y=-0.75;
    CSubtitle sa,sb;sa.m_wrapStyle=sb.m_wrapStyle=wrap;FwSTSStyle style={37};MicroString source(input.data(),static_cast<int>(input.size()));auto before=runUnits(source);bool ia=false,ib=false;
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();errno=EDOM;microHook=runHook;runMutation=mutation;runMutationCount=0;activeRunState=&a;
    try{a.ParseString(nullSub?NULL:&sa,source,style);}catch(const SearchInterrupted&){ia=true;}
    int ea=errno;unsigned ma=runMutationCount;auto events=searchEvents;searchAttempts=0;searchEvents.clear();errno=EDOM;runMutationCount=0;activeRunState=&b;microSide=1;
    try{b.ParseString(nullSub?NULL:&sb,source,style);}catch(const SearchInterrupted&){ib=true;}
    int eb=errno;unsigned mb=runMutationCount;microHook=NULL;
    if(ia!=ib || ea!=eb || ma!=mb || state(a)!=state(b) || sa.m_wrapStyle!=sb.m_wrapStyle || sa.m_words.snapshot()!=sb.m_words.snapshot() || events!=searchEvents || before!=runUnits(source)){
        fprintf(stderr,"Text run mismatch case=%llu length=%zu wrap=%d failure=%d mutation=%d interruption=%d/%d words=%zu/%zu callbacks=%zu/%zu\n",(unsigned long long)comparisons,input.size(),wrap,failure,mutation,ia,ib,sa.m_words.values.size(),sb.m_words.values.size(),events.size(),searchEvents.size());std::abort();
    }
    ++comparisons;words+=sa.m_words.values.size();callbacks+=events.size();interruptions+=ia;mutations+=ma;
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"",L"a",L" ",L"\n",L"\r",L"\t",L"\xa0",L"\\N",L"\\n",L"\\h",L"\\",L"N",L"n",L"h",L"  ",L"\n\n",L"{}"};
    const unsigned count=sizeof(pieces)/sizeof(*pieces);
    for(unsigned a=0;a<count;++a)for(unsigned b=0;b<count;++b)for(unsigned c=0;c<count;++c){std::wstring input=pieces[a];input+=pieces[b];input+=pieces[c];for(int wrap:{-1,0,1,2,3,4})compare(input,wrap);}
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring input=L"A";input.push_back(static_cast<wchar_t>(unit));input+=L" B\nC";compare(input,2);compare(input,3);}
    for(int wrap:{-3,0,1,2,3,4,INT32_MAX})for(int mutation=0;mutation<4;++mutation)for(int failure=-1;failure<24;++failure)compare(L"one two\\nthree\\Nfour\\hfive\n \tend",wrap,failure,mutation);
    for(int wrap=-2;wrap<5;++wrap)compare(L"one\\Ntwo",wrap,-1,0,true);
    for(unsigned length:{1023u,1024u,1025u,4095u,4096u,4097u}){std::wstring input(length,L'a');input+=L" \n tail";compare(input,0);}
    for(int mutation=1;mutation<4;++mutation){std::wstring input=L"one";input.push_back(0);input+=L" two\nthree";compare(input,2,-1,mutation);}
    if(liveRunAllocations || !words || !interruptions || !mutations || !runOwners || !runSteps){fprintf(stderr,"Text run route controls failed\n");return 1;}
    printf("%llu exact complete rendered-text splitting comparisons passed (%llu retained words, %llu callback observation values, %llu injected constructor/append interruptions, %llu live renderer-state mutations, %llu Rust owners / %llu token steps; every UTF-16 value, NUL tails, repeated spaces/newlines, literal tabs/NBSP, wrap-dependent escape preprocessing, null subtitle, exact word arguments/scaling bits and karaoke state; native word construction/list sinks modeled, replacement-bearing NUL fixtures and allocation failures excluded)\n",(unsigned long long)comparisons,(unsigned long long)words,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)runOwners,(unsigned long long)runSteps);
}
