// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_subrip.h"
static uint64_t comparisons=0,changed=0,units=0;
static std::vector<uint16_t> contents(const CStringW& text){std::vector<uint16_t> out;for(int i=0;i<text.GetLength();++i)out.push_back(static_cast<uint16_t>(text[i]));return out;}
static void compare(const std::wstring& input,int charset){
    CStringW source(input.data(),static_cast<int>(input.size()));auto before=contents(source);
    auto a=ReferenceSubRip::SubRipper2SSA(source,charset);auto b=CandidateSubRip::SubRipper2SSA(source,charset);
    auto reference=contents(a),candidate=contents(b);
    if(reference!=candidate || before!=contents(source)){fprintf(stderr,"SubRip mismatch case=%llu length=%zu charset=%d\n",(unsigned long long)comparisons,input.size(),charset);std::abort();}
    ++comparisons;changed+=reference!=before;units+=reference.size();
}
int main(){
    const std::wstring pieces[]={L"<i>",L"</i>",L"<b>",L"</b>",L"<u>",L"</u>",L"<I>",L"< i>",L"<",L">",L"/",L"i",L"<b<i>>",L"{\\i1}\\N"};
    const unsigned base=sizeof(pieces)/sizeof(*pieces);unsigned combinations=1;
    for(unsigned length=0;length<=4;++length){
        if(length)combinations*=base;
        for(unsigned value=0;value<combinations;++value){unsigned code=value;std::wstring input;
            for(unsigned i=0;i<length;++i){input+=pieces[code%base];code/=base;}
            compare(input,int(value%257)-1);
        }
    }
    for(unsigned unit=1;unit<=0xffff;++unit){std::wstring input=L"<i>";input.push_back(wchar_t(unit));input+=L"</i><U>";compare(input,128);}
    uint32_t random=0x72134951;
    for(unsigned value=0;value<14000;++value){std::wstring input;
        for(unsigned i=0;i<value%31;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=pieces[random%base];if(value%2==0)input.push_back(wchar_t(1+random%65535));}
        compare(input,int(value%65536));
    }
    for(unsigned value=0;value<256;++value){std::wstring input=L"before";input.push_back(0);input+=L"AFTER";input.push_back(wchar_t(value));compare(input,int(value));}
    std::wstring longInput;for(int i=0;i<12000;++i)longInput+=L"<i><b><u>x</u></b></i>";compare(longInput,1);
    // Exercise the real owner boundary independently of native fallback cases.
    const uint16_t nul[]={60,105,62,0};if(xy_vsfilter_subrip_create(nul,4)!=NULL){fprintf(stderr,"SubRip NUL route control failed\n");return 1;}
    const uint16_t tag[]={60,105,62};void* owner=xy_vsfilter_subrip_create(tag,3);
    if(!owner || xy_vsfilter_subrip_field(owner,0,0)!=5 || xy_vsfilter_subrip_field(owner,1,0)!=123 || !changed){fprintf(stderr,"SubRip conversion route control failed\n");return 1;}xy_vsfilter_subrip_release(owner);
    printf("%llu exact SubRip formatting comparisons passed (%llu changed strings, %llu compared UTF-16 units; six ordered case-sensitive replacements, every nonzero code unit, nested/malformed fragments, untouched source, ignored charset and stable NUL-without-tag fallback; tag-bearing NUL outcomes excluded)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)units);
}
