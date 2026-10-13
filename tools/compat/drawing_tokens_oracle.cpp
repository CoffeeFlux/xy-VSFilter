// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_drawing_tokens.h"
#include <cstdio>
#include <cstdlib>

static uint64_t comparisons=0;
static void check(const std::wstring& input) {
    DrawingString source(input.data(),static_cast<int>(input.size()));
    const auto original=OriginalDrawingTokens::Scan(source), candidate=CandidateDrawingTokens::Scan(source);
    if (original!=candidate) {
        fprintf(stderr,"Drawing token mismatch at comparison %llu, units:",static_cast<unsigned long long>(comparisons));
        for (wchar_t c:input) fprintf(stderr," %04x",static_cast<unsigned>(c));
        fprintf(stderr,"\nreference=%zu candidate=%zu\n",original.size(),candidate.size()); exit(1);
    }
    ++comparisons;
}
static void enumerate(std::wstring& input,int remaining) {
    check(input); if (!remaining) return;
    for (wchar_t c:{L'm',L'n',L's',L'p',L'c',L'*',L' ',L'\t',L'0',L'\0'}) {
        input.push_back(c); enumerate(input,remaining-1); input.pop_back();
    }
}
int main() {
    std::wstring input; enumerate(input,6);
    for (uint32_t c=0;c<=0xffff;++c) {
        std::wstring unit(1,static_cast<wchar_t>(c));
        check(unit+L"m 1 2"+unit+L"l 3 4*"+unit);
        check(L"m"+unit+L"*"+unit+L"n 5 6"+unit+L"s 1 2 3 4 5 6 c");
    }
    const std::wstring pieces[]={L"m 0 0",L"l 1 2",L"junk",L"0x1p+4",L"nan",L"M 1 2",L"****",L" ",L"\t",L"s p c",std::wstring(1,0),std::wstring(1,0xd800)};
    uint32_t random=0x95176abd;
    for (unsigned iteration=0;iteration<100000;++iteration) {
        input.clear();
        for (unsigned i=0,n=iteration%50;i<n;++i) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            input+=pieces[random%12];
        }
        check(input);
    }
    printf("%llu exact drawing-token comparisons passed (commands, raw parameter units, NUL handling and order)\n",static_cast<unsigned long long>(comparisons));
}
