// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include "original_font.h"

static uint64_t cases = 0;
static void compare(const std::vector<uint16_t>& input)
{
    CString text(input);
    OriginalFont::decoded.clear();
    CandidateFont::decoded.clear();
    const bool original = OriginalFont::LoadFont(text);
    const bool candidate = CandidateFont::LoadFont(text);
    ++cases;
    if (original != candidate || OriginalFont::decoded != CandidateFont::decoded) {
        fprintf(stderr, "Font decode mismatch case=%llu length=%zu\n",
            static_cast<unsigned long long>(cases), input.size());
        exit(1);
    }
}

int main()
{
    std::vector<uint16_t> quad(4);
    // Every effective six-bit lane combination. All 16-bit code units are
    // exercised separately below to verify subtraction/truncation behavior.
    for (uint32_t packed = 0; packed < 0x1000000; ++packed) {
        for (int i = 0; i < 4; ++i) quad[i] = 33 + ((packed >> (i * 6)) & 63);
        compare(quad);
    }
    for (uint32_t unit = 0; unit < 65536; ++unit) {
        for (size_t size = 0; size < 9; ++size)
            compare(std::vector<uint16_t>(size, static_cast<uint16_t>(unit)));
    }
    uint32_t random = 0xfade1234;
    for (size_t size = 0; size < 4097; ++size) {
        std::vector<uint16_t> input(size);
        for (size_t i = 0; i < size; ++i) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            input[i] = static_cast<uint16_t>(random);
        }
        compare(input);
    }
    printf("%llu exact font-attachment comparisons passed\n", static_cast<unsigned long long>(cases));
}
