// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include "original_text_reader.h"

static uint64_t lines = 0, files = 0, exceptions = 0;

static void check_state(const OriginalTextFile& original, const CandidateTextFile& candidate)
{
    if (original.Converted() != candidate.Converted() ||
        original.GetPosition() != candidate.GetPosition() || original.GetLength() != candidate.GetLength() ||
        static_cast<int>(original.m_encoding) != static_cast<int>(candidate.m_encoding) ||
        original.BufferPosition() != candidate.BufferPosition() || original.BufferCount() != candidate.BufferCount() ||
        original.read_requests != candidate.read_requests || original.diagnostics != candidate.diagnostics ||
        original.offset != candidate.offset || original.m_offset != candidate.m_offset) {
        fprintf(stderr, "State mismatch file=%llu comparison=%llu\n",
            static_cast<unsigned long long>(files), static_cast<unsigned long long>(lines));
        exit(1);
    }
}

static bool read_pair(OriginalTextFile& original, CandidateTextFile& candidate)
{
    bool a = false, b = false, threw_a = false, threw_b = false;
    try { a = original.ReadLine(); } catch (const std::runtime_error&) { threw_a = true; }
    try { b = candidate.ReadLine(); } catch (const std::runtime_error&) { threw_b = true; }
    ++lines;
    if (a != b || threw_a != threw_b) abort();
    if (threw_a) ++exceptions;
    check_state(original, candidate);
    return a;
}

static void compare(const std::vector<unsigned char>& input, int encoding,
                    size_t limit = 4096, size_t failure = 0, bool partial = false)
{
    OriginalTextFile original(input, encoding);
    CandidateTextFile candidate(input, encoding);
    if (original.Open("memory") != candidate.Open("memory")) abort();
    ++files;
    original.read_limit = candidate.read_limit = limit;
    original.fail_read = original.read_calls + failure;
    candidate.fail_read = candidate.read_calls + failure;
    original.partial_failure = candidate.partial_failure = partial;
    bool eof = false;
    size_t attempts = 0;
    while (!eof) {
        if (++attempts > input.size() + 2) abort();
        eof = read_pair(original, candidate);
    }
    // Incomplete tails can remain buffered at EOF. The next call must preserve
    // the same state rather than silently discarding or replacing that tail.
    for (int retry = 0; retry < 3; ++retry) read_pair(original, candidate);
    // Exercise the original logical seek calculation after decoding/fallback.
    for (int origin = 0; origin < 3; ++origin) {
        for (int offset = -2; offset < 8; ++offset) {
            if (original.Seek(offset, origin) != candidate.Seek(offset, origin)) abort();
            read_pair(original, candidate);
        }
    }
    // Reopen on an existing reader leaves an old cursor behind; change encoding
    // with unread buffered bytes as SetEncoding does, then resume decoding.
    original.read_limit = candidate.read_limit = 4096;
    if (original.Open("memory") != candidate.Open("memory")) abort();
    check_state(original, candidate);
    read_pair(original, candidate);
    original.m_encoding = static_cast<OriginalTextFile::enc>((encoding + 1) % 4);
    candidate.m_encoding = static_cast<CandidateTextFile::enc>((encoding + 1) % 4);
    read_pair(original, candidate);
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
    for (int encoding = 0; encoding < 4; ++encoding) {
        for (size_t limit = 1; limit <= 7; ++limit) {
            for (size_t failure = 0; failure <= 8; ++failure) {
                for (int partial = 0; partial < 2; ++partial) {
                    for (size_t probe = 0; probe < sizeof(probes) / sizeof(probes[0]); ++probe) {
                        std::vector<unsigned char> bytes(9, 'a');
                        bytes.insert(bytes.end(), probes[probe], probes[probe] + 4);
                        bytes.push_back('\n'); bytes.push_back(0xf0); bytes.push_back(0x9f);
                        compare(bytes, encoding, limit, failure, partial != 0);
                        bytes.insert(bytes.begin(), {0xef, 0xbb, 0xbf});
                        compare(bytes, encoding, limit, failure, partial != 0);
                    }
                }
            }
        }
    }
    printf("%llu text-file scenarios and %llu line/seek comparisons passed (%llu injected exceptions)\n",
        static_cast<unsigned long long>(files), static_cast<unsigned long long>(lines),
        static_cast<unsigned long long>(exceptions));
}
