// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_fields.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static uint64_t comparisons = 0;
static void check(const std::wstring& input, wchar_t separator) {
    // Explicit terminator followed by a guard; keep embedded NULs and raw units.
    std::vector<wchar_t> original(input.begin(), input.end());
    original.push_back(0); original.push_back(0xfefd);
    auto candidate = original;
    wchar_t* a = original.data(); wchar_t* b = candidate.data();
    for (unsigned step=0; step<6; ++step) {
        // Changing the separator also exercises cursor reuse after final fields.
        wchar_t sep = step < 3 ? separator : step == 3 ? L' ' : step == 4 ? L',' : 0;
        const wchar_t* ar = OriginalFields::TryNextStr(&a, sep);
        const wchar_t* br = CandidateFields::TryNextStr(&b, sep);
        if (original != candidate || a-original.data() != b-candidate.data() ||
            ar-original.data() != br-candidate.data()) {
            fprintf(stderr, "Field mismatch at comparison %llu, step %u, sep=%04x, input:",
                static_cast<unsigned long long>(comparisons), step, static_cast<unsigned>(sep));
            for (wchar_t unit : input) fprintf(stderr, " %04x", static_cast<unsigned>(unit));
            fprintf(stderr, "\n"); exit(1);
        }
        ++comparisons;
    }
}
static void enumerate(std::wstring& input, unsigned remaining) {
    for (wchar_t sep : {L',', L':', L' ', L'\0'}) check(input, sep);
    if (!remaining) return;
    for (wchar_t unit : {L'a', L'0', L' ', L'\t', L',', L':', L'\0'}) {
        input.push_back(unit); enumerate(input, remaining-1); input.pop_back();
    }
}
int main() {
    std::wstring input; enumerate(input,6);
    for (uint32_t unit=0; unit<=0xffff; ++unit) {
        wchar_t c = static_cast<wchar_t>(unit);
        check(std::wstring(L"  ")+c+L",\t"+c+L":end", c);
        check(std::wstring(1,c)+L"a, ,"+c, L',');
    }
    uint32_t random=0x735199da;
    const std::wstring pieces[]={L" ",L"\t",L",",L":",L"&hffff",L"0x80000000",L"nan",L"1.0e-4",L"Default",std::wstring(1,0),std::wstring(1,0xd800),std::wstring(1,0x2003)};
    for (unsigned iteration=0; iteration<10000; ++iteration) {
        input.clear();
        for (unsigned i=0,n=iteration%50; i<n; ++i) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            input+=pieces[random%12];
        }
        check(input,L',');
    }
    printf("%llu exact field comparisons passed (buffer mutation, returned pointer, next cursor, repeated reads and raw units)\n",static_cast<unsigned long long>(comparisons));
}
