// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_microdvd.h"
static uint64_t comparisons=0,changed=0,units=0,callbacks=0,interruptions=0,mutations=0,aliases=0;
static CStringW* mutatedSource=NULL;
static int mutationMode=0;
static void mutate(unsigned kind){
    if(kind!=4 || !mutationMode)return;
    if(mutationMode==1){std::wstring same(static_cast<size_t>(mutatedSource->GetLength()),L'X');*mutatedSource=CStringW(same.data(),static_cast<int>(same.size()));}
    else if(mutationMode==2)*mutatedSource=CStringW(L"source grew during native lowercase");
    else *mutatedSource=CStringW();
}
static std::vector<uint16_t> contents(const CStringW& text){std::vector<uint16_t> out;for(int i=0;i<text.GetLength();++i)out.push_back(static_cast<uint16_t>(text[i]));return out;}
static void compare(const std::wstring& input,const std::wstring& needle,const std::wstring& replacement,int alias=0,int failure=-1,int mutation=0){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW a(input.data(),static_cast<int>(input.size())),b=a,from(needle.data(),static_cast<int>(needle.size())),to(replacement.data(),static_cast<int>(replacement.size()));
    const auto before=contents(a),fromBefore=contents(from),toBefore=contents(to);
    bool ia=false,ib=false;searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();
    mutationMode=mutation;microHook=mutate;mutatedSource=&a;
    try{ReferenceMicroDVD::ReplaceNoCase(a,alias==1?a:from,alias==2?a:to);}catch(const SearchInterrupted&){ia=true;}
    auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;mutatedSource=&b;
    try{CandidateMicroDVD::ReplaceNoCase(b,alias==1?b:from,alias==2?b:to);}catch(const SearchInterrupted&){ib=true;}
    microHook=NULL;mutatedSource=NULL;auto output=contents(a);
    if(output!=contents(b) || ia!=ib || events!=searchEvents || fromBefore!=contents(from) || toBefore!=contents(to)){fprintf(stderr,"ReplaceNoCase mismatch case=%llu length=%zu needle=%zu replacement=%zu alias=%d failure=%d mutation=%d\n",(unsigned long long)comparisons,input.size(),needle.size(),replacement.size(),alias,failure,mutation);std::abort();}
    ++comparisons;changed+=output!=before;units+=output.size();callbacks+=events.size();interruptions+=ia;mutations+=mutation!=0&&!ia;aliases+=alias!=0;
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"A",L"a",L"AA",L"<I>",L"</i>",L"<b>",L"&nbsp;",L"&QUOT;",L"<br>",L" ",L"x"};
    const wchar_t* needles[]={L"a",L"aa",L"AA",L"<i>",L"</i>",L"&nbsp;",L"&quot;",L"<br>",L"<B>"};
    const wchar_t* outputs[]={L"",L"a",L"AAaa",L"{\\i1}",L" ",L"\\N"};
    const unsigned base=sizeof(pieces)/sizeof(*pieces);unsigned combinations=1;
    for(unsigned len=0;len<=4;++len){if(len)combinations*=base;for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;for(unsigned i=0;i<len;++i){input+=pieces[code%base];code/=base;}for(unsigned n=0;n<9;++n)compare(input,needles[n],outputs[value%6]);}}
    for(unsigned unit=1;unit<=0xffff;++unit){std::wstring input=L"<I>a";input.push_back(wchar_t(unit));input+=L"AA</i>";compare(input,L"<i>",L"{\\i1}");compare(input,L"aa",L"");}
    uint32_t random=0x35795246;
    for(unsigned value=0;value<12000;++value){std::wstring input;for(unsigned i=0;i<value%23;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=pieces[random%base];}compare(input,needles[value%9],outputs[value%6]);}
    for(unsigned i=0;i<256;++i){std::wstring input=L"before";input.push_back(0);input+=L"AFTER";compare(input,L"<i>",L"x");}
    for(const auto* piece:pieces)for(int alias=1;alias<=2;++alias)compare(piece,L"a",L"aa",alias);
    for(int mutation=0;mutation<=3;++mutation)for(int failure=-1;failure<=1;++failure)for(int alias=0;alias<=2;++alias)compare(L"<I>a</i>",L"<i>",L"{\\i1}",alias,failure,mutation);
    compare(L"",L"",L"x"); // Empty source terminates even with an empty needle.
    const uint16_t a[]={97};void* empty=xy_vsfilter_case_replace_create(a,1,a,1,NULL,0,a,1);
    if(empty){fprintf(stderr,"Empty-needle decline control failed\n");return 1;}
    if(!changed || !interruptions || !mutations || !aliases || !replaceHandled || !replaceFallbacks){fprintf(stderr,"ReplaceNoCase route controls failed\n");return 1;}
    printf("%llu exact case-insensitive replacement calls passed (%llu changed strings, %llu output UTF-16 units, %llu callback observation values, %llu injected interruptions, %llu source mutations, %llu source/argument aliases, %llu Rust conversions / %llu native fallbacks; native lowercase once, case-sensitive needle, nonrecursive replacements, every nonzero UTF-16 value, deletion/expansion and stable replacement-free NULs; nonterminating empty-needle and replacement-bearing NUL outcomes excluded)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)units,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)aliases,(unsigned long long)replaceHandled,(unsigned long long)replaceFallbacks);
}
