// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_find_char.h"
static uint64_t comparisons=0,found=0,callbacks=0,interruptions=0,translations=0;
static void compare(const std::wstring& input,wchar_t target,int pos,bool unicode,int charset,int mode=0,int failure=-1){
    CStringW str(input.data(),static_cast<int>(input.size()));
    searchMode=mode;searchFailure=failure;searchAttempts=0;searchEvents.clear();
    int a=-2,b=-2;bool interruptedA=false,interruptedB=false;
    try{a=ReferenceFindChar::FindChar(str,target,pos,unicode,charset);}catch(const SearchInterrupted&){interruptedA=true;}
    auto events=searchEvents;searchAttempts=0;searchEvents.clear();
    try{b=CandidateFindChar::FindChar(str,target,pos,unicode,charset);}catch(const SearchInterrupted&){interruptedB=true;}
    if(a!=b || interruptedA!=interruptedB || events!=searchEvents){
        fprintf(stderr,"FindChar mismatch case=%llu length=%zu target=%u pos=%d unicode=%d charset=%d mode=%d failure=%d results=%d/%d callbacks=%zu/%zu\n",(unsigned long long)comparisons,input.size(),unsigned(target),pos,unicode,charset,mode,failure,a,b,events.size(),searchEvents.size());std::abort();
    }
    ++comparisons;found+=a>=0;callbacks+=events.size();interruptions+=interruptedA;
    for(auto e:events)translations+=(e>>56)==1;
}
int main(){
    const wchar_t* fragments[]={L"",L"|",L"{}",L"{\\fe}",L"{\\fe128}",L"{\\fe0}",L"{{\\fe134}x}",L"{\\fe136}|{\\fe}",L"{\\FE128}|",L"{\\fe-1}|",L"\\fe128|",L"{e}|",L"{\\fe2147483647}|",L"{\\fe128",L"{\\fe12x34}|",L"{\\fe1}{\\fe2}|"};
    const int charsets[]={0,1,2,128,129,130,134,136,255,-1,65536};
    for(const auto* fragment:fragments){std::wstring s=fragment;
        for(int cs:charsets)for(int mode=0;mode<3;++mode)for(int pos=-2;pos<=int(s.size())+1;++pos)
            for(wchar_t c:{wchar_t(0),L'|',L'{',L'}',L'e',L'1',wchar_t(0xffff)}){
                compare(s,c,pos,false,cs,mode);
                if(pos>=0)compare(s,c,pos,true,cs,mode);
            }
    }
    // Every UTF-16 value is tested at a DBCS boundary and after an embedded NUL.
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring s=L"{\\fe128}";s.push_back(wchar_t(unit));s+=L"|";s.push_back(0);s+=L"x|";
        for(int mode=0;mode<3;++mode)compare(s,L'|',0,false,128,mode);
        compare(s,L'|',9,true,0);
        compare(s,wchar_t(unit),8,false,0);
    }
    for(int cs=0;cs<256;++cs)for(unsigned byte=0;byte<256;++byte){std::wstring s;s.push_back(wchar_t(byte));s+=L"|x|";compare(s,L'|',0,false,cs);}
    uint32_t random=0x75673129;
    for(unsigned n=0;n<16000;++n){std::wstring s;
        for(unsigned i=0;i<n%17;++i){random^=random<<13;random^=random>>17;random^=random<<5;s+=fragments[random%16];s.push_back(wchar_t(random%65536));}
        // Random concatenation can join a numeric suffix only after a complete
        // fragment; the only unterminated digit fragment has the bounded 128.
        compare(s,L'|',int(n%31)-2,false,charsets[n%11],int(n%3));
    }
    const std::wstring interrupted=L"{{\\fe128}a|{\\fe}b{\\fe134}c}d";
    for(int mode=0;mode<3;++mode)for(int failure=0;failure<80;++failure)compare(interrupted,L'?',0,false,128,mode,failure);
    if(!found || !interruptions || !translations || !rustSearches || !rustSearchSteps){fprintf(stderr,"FindChar route controls failed\n");return 1;}
    printf("%llu exact charset-aware FindChar comparisons passed (%llu found positions, %llu native callbacks, %llu code-page translations, %llu injected interruptions, %llu Rust searches / %llu steps; all UTF-16 values, all charset/byte pairs, embedded NULs, nested tags, empty charset reset, lead-byte skipping, Unicode native path and ordered classification replies; native arithmetic restricted to representable fixtures)\n",(unsigned long long)comparisons,(unsigned long long)found,(unsigned long long)callbacks,(unsigned long long)translations,(unsigned long long)interruptions,(unsigned long long)rustSearches,(unsigned long long)rustSearchSteps);
}
