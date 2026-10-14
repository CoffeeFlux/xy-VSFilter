// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_drawing_tokens.h"
#include "original_drawing_preparation.h"
#include <cstdio>
#include <cstdlib>

static uint64_t comparisons=0;
static void check(const std::wstring& input) {
    DrawingString source(input.data(),static_cast<int>(input.size()));
    const auto original=OriginalDrawingTokens::Scan(source), candidate=CandidateDrawingTokens::Scan(source);
    if (original!=candidate) {
        fprintf(stderr,"Drawing token mismatch at comparison %llu, units:",static_cast<unsigned long long>(comparisons));
        for (wchar_t c:input) fprintf(stderr," %04x",static_cast<unsigned>(c));
        fprintf(stderr,"\nreference=%zu candidate=%zu\n",original.size(),candidate.size());
        for (const auto* list : {&original, &candidate}) {
            fprintf(stderr, "%s tokens:\n", list == &original ? "reference" : "candidate");
            for (const auto& token : *list) {
                fprintf(stderr, "  command=%04x parameters:", token.command);
                for (uint16_t c : token.parameters) fprintf(stderr, " %04x", c);
                fprintf(stderr, "\n");
            }
        }
        exit(1);
    }
    ++comparisons;
}
static uint64_t preparedComparisons=0,preparedTokens=0;
static void checkPrepared(const std::wstring& input){
    DrawingString source(input.data(),static_cast<int>(input.size()));
    auto original=OriginalDrawingPreparation::Scan(source),candidate=CandidateDrawingPreparation::Scan(source);
    if(original!=candidate){fprintf(stderr,"Drawing preparation mismatch case=%llu length=%zu tokens=%zu/%zu\n",(unsigned long long)preparedComparisons,input.size(),original.size(),candidate.size());std::abort();}
    ++preparedComparisons;preparedTokens+=original.size();
}
static void enumerate(std::wstring& input,int remaining) {
    check(input); if (!remaining) return;
    for (wchar_t c:{L'm',L'n',L's',L'p',L'c',L'*',L' ',L'\t',L'0',L'\0'}) {
        input.push_back(c); enumerate(input,remaining-1); input.pop_back();
    }
}
int main() {
    // Raw post-replacement buffers: replacement itself remains native because
    // embedded NULs can expose allocation-dependent contents (see evidence).
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
    const std::wstring rawPieces[]={L"m 1 2",L"n",L"b",L"s",L"p",L"c",L"l",L"junk",L"0x1p+4",L"M N",L"**",L" ",L"\t",L"\r\n"};
    for(const auto& a:rawPieces)for(const auto& b:rawPieces)for(const auto& c:rawPieces)checkPrepared(a+b+c);
    for(uint32_t unit=1;unit<=0xffff;++unit){std::wstring value(1,static_cast<wchar_t>(unit));checkPrepared(value+L"m 1 2"+value+L"l 3 4*"+value);checkPrepared(L"m"+value+L"*"+value+L"n 5 6"+value+L"s 1 2 3 4 5 6 c");}
    for(unsigned iteration=0;iteration<100000;++iteration){input.clear();for(unsigned i=0,n=iteration%50;i<n;++i){random^=random<<13;random^=random>>17;random^=random<<5;input+=rawPieces[random%14];}checkPrepared(input);}
    // NUL fallbacks without command replacements have defined counted input.
    for(const auto& text:{std::wstring(1,0),std::wstring(L"X\0Y",3),std::wstring(L"**X\0Y",5)})checkPrepared(text);
    if(!preparedTokens || !drawingPreparations || !drawingFallbacks || drawingFallbacks!=drawingResets){fprintf(stderr,"Drawing preparation route controls failed\n");return 1;}
    printf("%llu exact drawing-preparation comparisons passed (%llu emitted tokens, %llu Rust preparations / %llu native fallbacks / %llu reimports; all nonzero UTF-16 units, command letters inside arbitrary text, existing separators, raw surrogates and replacement-free NUL fallback; replacement-bearing NUL cases and allocation failures excluded)\n",(unsigned long long)preparedComparisons,(unsigned long long)preparedTokens,(unsigned long long)drawingPreparations,(unsigned long long)drawingFallbacks,(unsigned long long)drawingResets);
    printf("%llu exact drawing-token comparisons passed (commands, raw parameter units, NUL handling and order)\n",static_cast<unsigned long long>(comparisons));
}
