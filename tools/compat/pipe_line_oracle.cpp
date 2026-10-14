// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_pipe_line.h"
static uint64_t comparisons=0,accepted=0,rejected=0,interruptions=0,callbacks=0,records=0,changed=0;
static void compare(const std::vector<SamiLine>& input,bool old,bool unicode,int charset,int failure=-1,int mode=0){
    microCase=comparisons;microSide=0;microInput=L"";microLength=0;
    CTextFile a(input,unicode,0),b(input,unicode,0);CSimpleTextSubtitle sa,sb;
    bool ra=false,rb=false,ia=false,ib=false;
    searchFailure=failure;searchMode=mode;searchAttempts=0;searchEvents.clear();errno=EDOM;
    try{ra=old?ReferencePipeLine::OpenOldSubRipper(&a,sa,charset):ReferencePipeLine::OpenVPlayer(&a,sa,charset);}catch(const SearchInterrupted&){ia=true;}
    int ea=errno;auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;errno=EDOM;
    try{rb=old?CandidatePipeLine::OpenOldSubRipper(&b,sb,charset):CandidatePipeLine::OpenVPlayer(&b,sb,charset);}catch(const SearchInterrupted&){ib=true;}
    int eb=errno;
    if(ra!=rb || ia!=ib || ea!=eb || a.position!=b.position || sa.entries!=sb.entries || events!=searchEvents){
        fprintf(stderr,"Pipe line mismatch case=%llu old=%d wide=%d charset=%d mode=%d failure=%d accepted=%d/%d interrupted=%d/%d position=%zu/%zu records=%zu/%zu callbacks=%zu/%zu errno=%d/%d\n",(unsigned long long)comparisons,old,unicode,charset,mode,failure,ra,rb,ia,ib,a.position,b.position,sa.entries.size(),sb.entries.size(),events.size(),searchEvents.size(),ea,eb);std::abort();
    }
    ++comparisons;accepted+=ra;rejected+=!ra&&!ia;interruptions+=ia;callbacks+=events.size();records+=sa.entries.size();
    for(const auto& entry:sa.entries)for(auto unit:entry.text)changed+=unit==10;
}
int main(){
    microDiagnostics();
    const wchar_t* payloads[]={L"",L"text",L"|",L"a|b|",L"||",L"{\\fe128}a|b",L"{\\fe}a|b",L"{\\fe0}a|b",L"{\\FE128}a|b",L"{\\fe128x}a|b",L"{open|",L"}close|",L"\x81|x|y",L"{\\fe134}\x81|x|y",L"{\\fe128}{\\fe0}a|b",L"a\0tail"};
    const wchar_t* oldHeaders[]={L"{0:0:0}{0:0:4}",L"{0:1:2}{0:1:4",L"{0:0:0}{0:0:",L"{0:0:0}",L"junk",L"",L"{ -1:2:3}{0:1:4}",L"{0:0:0}{0:0:0}"};
    const wchar_t* newHeaders[]={L"0:0:0:",L"0:1:2",L"0:0:",L"0:0",L"junk",L"",L"-1:2:3:",L"0:0:4:"};
    for(int old=0;old<2;++old){
        const auto* headers=old?oldHeaders:newHeaders;
        for(auto headerIndex=0;headerIndex<8;++headerIndex)for(auto payload:payloads)for(int charset=0;charset<256;++charset){
            std::wstring input=headers[headerIndex];input+=payload;
            compare({{input,true}},old,charset&1,charset);
        }
        for(unsigned unit=0;unit<=0xffff;++unit){
            std::wstring text=headers[0];text+=L"a";text.push_back(static_cast<wchar_t>(unit));text+=L"|b|";
            uint64_t before=accepted;
            compare({{text,true}},old,true,128);
            compare({{text,true}},old,false,128);
            // macOS swscanf may reject the entire wide input in the C locale
            // when a later payload unit cannot be represented. Keep that native
            // result; verify the valid-header acceptance control on ASCII here.
            if(unit<128 && accepted!=before+2){fprintf(stderr,"Valid-header route rejected old=%d unit=%u\n",old,unit);return 1;}
        }
        std::wstring first=headers[0];first+=L"{\\fe128}\x81|one|two";
        std::wstring second=headers[0];second+=L"last|line";
        std::vector<SamiLine> lines={{L"",true},{first,true},{L" \t",true},{second,true},{L"junk",true}};
        for(int mode=0;mode<3;++mode)for(int failure=-1;failure<100;++failure)compare(lines,old,false,128,failure,mode);
        for(size_t i=0;i<lines.size();++i){auto copy=lines;copy[i].success=false;compare(copy,old,false,128);}
        compare({},old,true,0);
    }
    if(!accepted || !rejected || !interruptions || !changed || !pipeOwners || !pipeReplies || !rustUnicodeSearches || !rustSearches){fprintf(stderr,"Pipe line route controls failed\n");return 1;}
    printf("%llu exact legacy SubRip/VPlayer file comparisons passed (%llu accepted / %llu rejected / %llu injected interruptions, %llu Add records / %llu emitted newlines, %llu native callback observation values, %llu Rust line owners / %llu search replies; every UTF-16 value in Unicode and packed-byte mode, all caller charsets, DBCS and charset tags, missing delimiters, partial scans, failed reads and callback order; controlled file/sink boundaries and representable numeric arithmetic)\n",(unsigned long long)comparisons,(unsigned long long)accepted,(unsigned long long)rejected,(unsigned long long)interruptions,(unsigned long long)records,(unsigned long long)changed,(unsigned long long)callbacks,(unsigned long long)pipeOwners,(unsigned long long)pipeReplies);
}
