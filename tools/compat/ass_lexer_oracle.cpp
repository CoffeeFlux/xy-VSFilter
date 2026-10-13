// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ass_lexer.h"
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

static uint64_t comparisons = 0;
static void check(const std::wstring& input) {
    CStringW text(input);
    AssTagList original, candidate;
    if (!OriginalLexer::ParseSSATag(&original, text) || !CandidateLexer::ParseSSATag(&candidate, text)
        || original != candidate) {
        std::fprintf(stderr, "ASS lexer mismatch at comparison %llu, units:",
            static_cast<unsigned long long>(comparisons));
        for (wchar_t unit : input) std::fprintf(stderr, " %04x", static_cast<unsigned>(unit));
        std::fprintf(stderr, "\n");
        std::exit(1);
    }
    ++comparisons;
}
static uint32_t state = 0x73547bc1;
static uint32_t random32() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
static void enumerate(std::wstring& input, int remaining) {
    check(input);
    if (!remaining) return;
    for (wchar_t c : {L'\\', L'(', L')', L',', L' ', L'\t', L'b', L'\0'}) {
        input.push_back(c); enumerate(input, remaining - 1); input.pop_back();
    }
}
int main() {
    std::wstring input;
    enumerate(input, 7);
    // Every UTF-16 unit in command, trim, and parameter positions. Re-run with
    // another available CRT locale to verify classification stays native.
    for (const char* locale : {"C", ".UTF-8", "C.UTF-8", "en_US.UTF-8"}) {
        if (!std::setlocale(LC_CTYPE, locale)) continue;
        for (uint32_t unit = 0; unit <= 0xffff; ++unit) {
            std::wstring c(1, static_cast<wchar_t>(unit));
            check(L"\\" + c + L"b" + c + L"(" + c + L"1" + c + L",," + c + L"\\i1,2" + c + L")\\");
        }
    }
    std::setlocale(LC_CTYPE, "C");
    const std::wstring pieces[] = {L"\\t(0,100,\\bord2,\\t(\\blur4))", L"\\pos(1,2)",
        L"\\", L"\\ ", L" ", L"\t", L"\r\n", L"(", L")", L",", L"&Hff&", L"\\unknown",
        L"\\fnComic Sans", L"\\1c&H00FF00&", L"\\fscx-1", std::wstring(1, 0),
        std::wstring(1, 0xd800), std::wstring(1, 0x2003)};
    for (unsigned iteration = 0; iteration < 200000; ++iteration) {
        input.clear();
        const unsigned count = random32() % 80;
        for (unsigned i = 0; i < count; ++i) {
            if ((random32() & 7) == 0) input.push_back(static_cast<wchar_t>(random32() & 0xffff));
            else input += pieces[random32() % (sizeof(pieces) / sizeof(pieces[0]))];
        }
        check(input);
    }
    check(std::wstring(32768, L'\\'));
    check(L"\\t(" + std::wstring(32768, L',') + L"\\b2)");
    std::printf("%llu exact ASS lexer comparisons passed (command, parameter units, order and empty tags).\n",
        static_cast<unsigned long long>(comparisons));
}
