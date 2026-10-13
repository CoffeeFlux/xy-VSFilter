// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_fields.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <clocale>

static uint64_t comparisons = 0;
static uint64_t string_comparisons = 0;
static bool equal(const FieldString& a, const FieldString& b) {
    if (a.GetLength()!=b.GetLength()) return false;
    for (int i=0; i<a.GetLength(); ++i) if (a[i]!=b[i]) return false;
    return true;
}
static void check(const std::wstring& input, wchar_t separator) {
    // Explicit terminator followed by a guard; keep embedded NULs and raw units.
    std::vector<wchar_t> original(input.begin(), input.end());
    original.push_back(0); original.push_back(0xfefd);
    auto candidate = original;
    FieldString astr(input.data(),static_cast<int>(input.size())), bstr=astr;
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
        FieldString afield, bfield; int aerror=0, berror=0;
        try { afield=OriginalFields::GetStr(astr,sep); } catch (int e) { aerror=e; }
        try { bfield=CandidateFields::GetStr(bstr,sep); } catch (int e) { berror=e; }
        if (!equal(astr,bstr) || !equal(afield,bfield) || aerror!=berror) {
            fprintf(stderr,"CString field mismatch at comparison %llu, step %u, sep=%04x, errors=%d/%d, buffer lengths=%d/%d, field lengths=%d/%d, input:",
                static_cast<unsigned long long>(string_comparisons), step, static_cast<unsigned>(sep), aerror,berror,astr.GetLength(),bstr.GetLength(),afield.GetLength(),bfield.GetLength());
            for (wchar_t unit : input) fprintf(stderr," %04x",static_cast<unsigned>(unit));
            fprintf(stderr,"\n"); exit(1);
        }
        ++string_comparisons;
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
    // Use the actual CRT classification in each available locale, never a Rust
    // Unicode whitespace table. C is always available; other locales may not be.
    for (const char* locale : {"C", "", ".1252"}) {
        if (!std::setlocale(LC_CTYPE,locale)) continue;
        for (uint32_t unit=0; unit<=0xffff; ++unit) {
            wchar_t c = static_cast<wchar_t>(unit);
            check(std::wstring(L"  ")+c+L",\t"+c+L":end", c);
            check(std::wstring(1,c)+L"a, ,"+c, L',');
        }
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
    printf("%llu exact CString field comparisons passed (trimmed buffer, returned raw units, exceptions, repeated reads and native locale whitespace)\n",static_cast<unsigned long long>(string_comparisons));
}
