// SPDX-License-Identifier: GPL-2.0-or-later
// original_next_char.h is generated from the PINNED original C++ source.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "original_next_char.h"
#include "xy_rust_text.h"

static uint64_t comparisons = 0;

static void compare(uint32_t packed, size_t length, int encoding, size_t start = 0)
{
    CTextFile reference;
    memset(reference.m_readbuffer, 0xa5, sizeof(reference.m_readbuffer));
    for (size_t i = 0; i < length; ++i)
        reference.m_readbuffer[start + i] = static_cast<unsigned char>(packed >> (8 * i));
    reference.m_bufferPos = start;
    reference.m_bufferCount = start + length;
    reference.m_encoding = static_cast<CTextFile::enc>(encoding);
    size_t position = start;
    const int expected = reference.NextChar();
    const int actual = XyRust::NextChar(reference.m_readbuffer, reference.m_bufferCount, position, encoding);
    ++comparisons;
    if (actual != expected || position != reference.m_bufferPos) {
        fprintf(stderr, "Mismatch packed=%08x length=%zu encoding=%d start=%zu: "
            "C++=(%d,%zu) Rust=(%d,%zu)\n", packed, length, encoding, start,
            expected, reference.m_bufferPos, actual, position);
        exit(1);
    }
}

int main()
{
    // Complete domains for default bytes, UTF-16 units, lead-byte decisions,
    // and every possible tail byte (including malformed ones) of UTF-8.
    for (int encoding = 0; encoding < 4; ++encoding) {
        for (uint32_t b = 0; b < 256; ++b) {
            compare(b, 1, encoding);
            compare(b, 1, encoding, 3);
        }
    }
    for (uint32_t word = 0; word < 65536; ++word) {
        compare(word, 2, 0);
        compare(word, 2, 1);
        compare(word, 2, 2);
        compare(word, 2, 3);
        compare(word, 2, 1, 3);
    }
    for (uint32_t lead = 0xe0; lead < 0xf5; ++lead) {
        for (uint32_t tail = 0; tail < 65536; ++tail)
            compare(lead | (tail << 8), 3, 1);
    }
    for (uint32_t lead = 0xf0; lead < 0xf5; ++lead) {
        for (uint32_t tail = 0; tail < 0x1000000; ++tail)
            compare(lead | (tail << 8), 4, 1);
    }
    // Nonzero cursors and trailing bytes. Unknown encodings are not a defined
    // C++ enum value, so test them only on the Rust side (never as an oracle).
    uint32_t random = 0x78563412;
    for (uint32_t i = 0; i < 100000; ++i) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        for (int encoding = 0; encoding < 4; ++encoding)
            compare(random, 4, encoding, 3);
    }
    // Empty/invalid cursors cannot be passed safely to the old default/UTF8
    // decoder. Exercise only the new defensive behavior here.
    size_t position = 0;
    if (XyRust::NextChar(NULL, 0, position, 1) != -1 || position != 0) return 2;
    position = 1;
    if (XyRust::NextChar(NULL, 0, position, 1) != -1 || position != 1) return 2;
    printf("%llu exact decoder comparisons passed\n", static_cast<unsigned long long>(comparisons));
    return 0;
}
