// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_subrip_cue.h"
static uint64_t comparisons=0,accepted=0,rejected=0,interruptions=0,callbacks=0,records=0;
static void compare(const std::vector<SamiLine>& input,bool unicode=true,size_t initial=0,int failure=-1){
    microCase=comparisons;microSide=0;microInput=L"";microLength=0;
    CTextFile a(input,unicode,initial),b(input,unicode,initial);CSimpleTextSubtitle sa,sb;
    bool ra=false,rb=false,ia=false,ib=false;
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();errno=EDOM;
    try{ra=ReferenceSubRipCue::OpenSubRipper(&a,sa,128);}catch(const SearchInterrupted&){ia=true;}
    int ea=errno;auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;errno=EDOM;
    try{rb=CandidateSubRipCue::OpenSubRipper(&b,sb,128);}catch(const SearchInterrupted&){ib=true;}
    int eb=errno;
    if(ra!=rb || ia!=ib || ea!=eb || a.position!=b.position || sa.entries!=sb.entries || events!=searchEvents){
        fprintf(stderr,"SubRip cue mismatch case=%llu lines=%zu initial=%zu failure=%d accepted=%d/%d interrupted=%d/%d position=%zu/%zu records=%zu/%zu callbacks=%zu/%zu errno=%d/%d\n",(unsigned long long)comparisons,input.size(),initial,failure,ra,rb,ia,ib,a.position,b.position,sa.entries.size(),sb.entries.size(),events.size(),searchEvents.size(),ea,eb);std::abort();
    }
    ++comparisons;accepted+=ra;rejected+=!ra&&!ia;interruptions+=ia;callbacks+=events.size();records+=sa.entries.size();
}
int main(){
    microDiagnostics();
    const wchar_t* timing=L"00:00:00,000 --> 00:00:04,000";
    const wchar_t* pieces[]={L"",L" ",L"\t",L"1",L"-2",L"+3",L"4tail",L"1  ",L"plain",L"<i>italic</i>",L"<b>bold",L"<u>under</u>",L"00:00:00,000 --> 00:00:04,000",L"0:1:2 --> 0:1:4",L"0;1;2,30 --> 0;1;4,5",L"0:1:2,000 --> 0:1:4",L"0:1:2,1234 --> 0:1:4,5",L"-1:0:0,000 --> 0:0:4,000"};
    const unsigned count=sizeof(pieces)/sizeof(*pieces);
    compare({});
    for(unsigned a=0;a<count;++a)for(unsigned b=0;b<count;++b)for(unsigned c=0;c<count;++c){
        std::vector<SamiLine> lines={{pieces[a],true},{pieces[b],true},{pieces[c],true}};
        compare(lines,(a&1)!=0);lines.insert(lines.begin(),{timing,true});compare(lines,(b&1)!=0);
    }
    for(unsigned unit=0;unit<=0xffff;++unit){
        std::wstring body=L"raw";body.push_back(static_cast<wchar_t>(unit));body+=L"data";
        compare({{L"1",true},{timing,true},{body,true},{L"",true},{L"2",true},{timing,true},{L"tail",true}});
        std::wstring numeric=L"2";numeric.push_back(static_cast<wchar_t>(unit));
        compare({{timing,true},{L"first",true},{L"",true},{L"not a number",true},{numeric,true},{timing,true},{L"last",true}},false);
    }
    const std::vector<SamiLine> special={{L"1",true},{timing,true},{L"first",true},{L"",true},{L"intervening",true},{L"2",true},{timing,true},{L"<i>second</i>",true},{L"",true},{L"3",true},{L"junk",true}};
    for(int failure=0;failure<180;++failure)compare(special,true,0,failure);
    for(size_t initial=0;initial<=special.size();++initial)compare(special,false,initial);
    for(size_t final=0;final<special.size();++final){auto lines=special;lines[final].success=false;compare(lines);}
    compare({{timing,true},{L"final failed read",false}});
    compare({{timing,true},{L"",false}});
    if(!accepted || !rejected || !interruptions || !cueOwners || !cueEmpty || !cueBoundaries || !cueAppends){fprintf(stderr,"SubRip cue route controls failed\n");return 1;}
    printf("%llu exact complete SubRip file comparisons passed (%llu accepted / %llu rejected / %llu injected interruptions, %llu Add records, %llu native callback observation values, %llu Rust cue owners / %llu empty-line notices / %llu boundary decisions / %llu appended fragments; persistent blank-line state, numeric-line consumption, failed reads carrying text, raw UTF-16/NULs, partial scans, native timestamp conversion and retained captions; controlled file/sink boundaries, representable numeric scans and replacement-free NUL fixtures)\n",(unsigned long long)comparisons,(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)interruptions,(unsigned long long)records,(unsigned long long)callbacks,(unsigned long long)cueOwners,(unsigned long long)cueEmpty,(unsigned long long)cueBoundaries,(unsigned long long)cueAppends);
}
