// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_sami_file.h"
static uint64_t comparisons=0,accepted=0,rejected=0,interruptions=0,callbacks=0,records=0;
static void compare(const std::vector<SamiLine>& input,bool unicode=true,size_t initial=0,int failure=-1){
    microCase=comparisons;microSide=0;microInput=L"";microLength=0;
    CTextFile a(input,unicode,initial),b(input,unicode,initial);CSimpleTextSubtitle sa,sb;
    bool ra=false,rb=false,ia=false,ib=false;
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();errno=EDOM;
    try{ra=ReferenceSami::OpenSami(&a,sa,128);}catch(const SearchInterrupted&){ia=true;}
    int ea=errno;auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;errno=EDOM;
    try{rb=CandidateSami::OpenSami(&b,sb,128);}catch(const SearchInterrupted&){ib=true;}
    int eb=errno;
    if(ra!=rb || ia!=ib || ea!=eb || a.position!=b.position || sa.entries!=sb.entries || events!=searchEvents){
        fprintf(stderr,"SAMI file mismatch case=%llu lines=%zu initial=%zu failure=%d accepted=%d/%d interrupted=%d/%d position=%zu/%zu records=%zu/%zu callbacks=%zu/%zu errno=%d/%d\n",(unsigned long long)comparisons,input.size(),initial,failure,ra,rb,ia,ib,a.position,b.position,sa.entries.size(),sb.entries.size(),events.size(),searchEvents.size(),ea,eb);std::abort();
    }
    ++comparisons;accepted+=ra;rejected+=!ra&&!ia;interruptions+=ia;callbacks+=events.size();records+=sa.entries.size();
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"",L" ",L"<SAMI>",L"<sami>",L"plain",L"<SYNC START=0>",L"<SYNC START=23ms>",L"<sync start=1a2b3>",L"prefix<SYNC START=20>",L"0000000000001<SYNC START=20>",L"<!--",L"-->",L"<!--closed-->",L"<TITLE>",L"</TITLE>",L"<title>title</title><SYNC START=9>",L"<P CLASS=x>text",L"<i>italic</i>",L"<font color=red>red</font>",L"&nbsp;",L"<SYNC START=M20>",L"<SYNC START=2>after<!--"};
    const unsigned count=sizeof(pieces)/sizeof(*pieces);
    compare({});compare({{L"not sami",true},{L"<SAMI>",false}});
    for(unsigned a=0;a<count;++a)for(unsigned b=0;b<count;++b)for(unsigned c=0;c<count;++c){
        std::vector<SamiLine> lines={{pieces[a],true},{pieces[b],true},{pieces[c],true}};
        compare(lines,(a&1)!=0);lines.insert(lines.begin(),{L"<SAMI>",true});compare(lines,(b&1)!=0);
    }
    for(unsigned unit=0;unit<=0xffff;++unit){
        std::wstring body=L"raw";body.push_back(static_cast<wchar_t>(unit));body+=L"data";
        compare({{L"<SAMI>",true},{L"<SYNC START=10>",true},{body,true},{L"<SYNC START=20>",true},{L"last",true}});
        std::wstring sync=L"<SYNC START=";sync.push_back(static_cast<wchar_t>(unit));sync+=L"2>";
        compare({{L"ignored",true},{L"<SAMI>",true},{sync,true},{L"tail",true}},false,1);
    }
    const std::vector<SamiLine> special={{L"<sami>",true},{L"<!--",true},{L"<SYNC START=77>",true},{L"-->",true},{L"<SYNC START=10>",true},{L"<font color=red>caption</font>",true},{L"<TITLE>skip</TITLE>",true},{L"<SYNC START=20>",true},{L"tail",true}};
    for(int failure=0;failure<240;++failure)compare(special,true,0,failure);
    for(size_t initial=0;initial<=special.size();++initial)compare(special,false,initial);
    for(size_t final=0;final<special.size();++final){auto lines=special;lines[final].success=false;compare(lines);}
    for(unsigned i=0;i<8;++i){
        std::wstring marker=i&1?L"<!--":L"<TITLE>";marker.push_back(0);marker+=i&2?L"-->":L"</TITLE>";
        compare({{L"<SAMI>",true},{marker,true},{L"<SYNC START=20>",true},{L"-->",true},{L"tail",true}});
    }
    if(!accepted || !rejected || !interruptions || !fileOwners || !fileBegins || !fileAppends || !fileFlushes || !fileEnds){fprintf(stderr,"SAMI file route controls failed\n");return 1;}
    printf("%llu exact complete SAMI file comparisons passed (%llu accepted / %llu rejected / %llu injected interruptions, %llu Add records, %llu native callback observation values, %llu Rust owners / %llu line classifications / %llu appended fragments / %llu completed flushes / %llu line endings; probe read-ahead and seek, comments/title precedence, timestamp scan from absolute index 12, final Add, every UTF-16 value, counted NULs and failed reads; controlled file/sink boundaries, representable timestamp arithmetic and replacement-free NUL fixtures)\n",(unsigned long long)comparisons,(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)interruptions,(unsigned long long)records,(unsigned long long)callbacks,(unsigned long long)fileOwners,(unsigned long long)fileBegins,(unsigned long long)fileAppends,(unsigned long long)fileFlushes,(unsigned long long)fileEnds);
}
