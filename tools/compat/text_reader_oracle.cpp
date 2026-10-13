// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include "original_text_reader.h"

static uint64_t lines = 0, files = 0;

static void compare(const std::vector<unsigned char>& input, int encoding)
{
    OriginalTextFile original(input, encoding);
    CandidateTextFile candidate(input, encoding);
    if (original.Open("memory") != candidate.Open("memory")) abort();
    ++files;
    bool eof = false;
    size_t attempts = 0;
    while (!eof) {
        if (++attempts > input.size() + 2) abort();
        eof = original.ReadLine();
        const bool actual_eof = candidate.ReadLine();
        ++lines;
        if (actual_eof != eof || original.m_convertedBuffer != candidate.m_convertedBuffer ||
            original.GetPosition() != candidate.GetPosition() || original.GetLength() != candidate.GetLength() ||
            static_cast<int>(original.m_encoding) != static_cast<int>(candidate.m_encoding) ||
            original.m_bufferPos != candidate.m_bufferPos || original.m_bufferCount != candidate.m_bufferCount) {
            fprintf(stderr, "Line mismatch file=%llu line=%zu size=%zu encoding=%d\n",
                    static_cast<unsigned long long>(files), attempts, input.size(), encoding);
            exit(1);
        }
    }
    // Exercise the original logical seek calculation after decoding/fallback.
    for (int origin = 0; origin < 3; ++origin) {
        for (int offset = -2; offset < 8; ++offset) {
            if (original.Seek(offset, origin) != candidate.Seek(offset, origin)) abort();
            const bool a = original.ReadLine(), b = candidate.ReadLine();
            ++lines;
            if (a != b || original.m_convertedBuffer != candidate.m_convertedBuffer ||
                original.GetPosition() != candidate.GetPosition()) abort();
        }
    }
}

int main()
{
    uint32_t random = 0x1234abcd;
    for (int size = 0; size < 20; ++size) {
        for (int byte = 0; byte < 256; ++byte) {
            for (int encoding = 0; encoding < 4; ++encoding)
                compare(std::vector<unsigned char>(size, static_cast<unsigned char>(byte)), encoding);
        }
    }
    for (int count = 0; count < 1000; ++count) {
        size_t size = static_cast<size_t>(count * 13);
        std::vector<unsigned char> bytes(size);
        for (size_t index = 0; index < size; ++index) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            bytes[index] = static_cast<unsigned char>(random);
        }
        for (int encoding = 0; encoding < 4; ++encoding) compare(bytes, encoding);
    }
    const unsigned char probes[][4] = {
        {0xc2, 0x41, '\r', '\n'}, {0xf0, 0x9f, 0x98, 0x80},
        {0xf4, 0xbf, 0xbf, 0xbf}, {0xed, 0xa0, 0x80, '\n'},
        {0xc0, 0x80, '\r', '\n'}, {0xe0, 0x80, 0x80, '\n'}
    };
    for (size_t offset = 4090; offset < 4102; ++offset) {
        for (size_t length = 0; length <= 4; ++length) {
            for (size_t probe = 0; probe < sizeof(probes) / sizeof(probes[0]); ++probe) {
                for (int bom = 0; bom < 4; ++bom) {
                    std::vector<unsigned char> bytes(offset, 'a');
                    if (bom == 1) { bytes[0] = 0xef; bytes[1] = 0xbb; bytes[2] = 0xbf; }
                    if (bom == 2) { bytes[0] = 0xff; bytes[1] = 0xfe; }
                    if (bom == 3) { bytes[0] = 0xfe; bytes[1] = 0xff; }
                    bytes.insert(bytes.end(), probes[probe], probes[probe] + length);
                    for (int encoding = 0; encoding < 4; ++encoding) compare(bytes, encoding);
                }
            }
        }
    }
    printf("%llu text-file scenarios and %llu line/seek comparisons passed\n",
        static_cast<unsigned long long>(files), static_cast<unsigned long long>(lines));
}
