// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

// Internal symbols, not DLL exports. C ABI uses values only, including on x86.
#ifdef _MSC_VER
#define XY_RUST_CDECL __cdecl
#else
#define XY_RUST_CDECL
#endif
extern "C" uint64_t XY_RUST_CDECL xy_vsfilter_next_char(
    uint32_t packed, uint32_t available, uint32_t encoding);
extern "C" uint32_t XY_RUST_CDECL xy_vsfilter_decode_font_group(uint64_t packed);
#undef XY_RUST_CDECL

namespace XyRust {
// The LoadFont caller owns nonoverlapping input/output arrays of length units
// and length bytes respectively. Packing by value avoids a Rust pointer ABI.
template<typename Unit>
inline int DecodeFont(const Unit* input, int length, unsigned char* output)
{
    if (!input || !output || length <= 0 || (length & 3) == 1) return -1;
    int written = 0;
    for (int offset = 0; offset < length; ) {
        const int count = length - offset < 4 ? length - offset : 4;
        uint64_t packed = 0;
        for (int i = 0; i < count; ++i)
            packed |= static_cast<uint64_t>(static_cast<uint16_t>(input[offset + i])) << (i * 16);
        const uint32_t decoded = xy_vsfilter_decode_font_group(packed);
        for (int i = 0; i < count - 1; ++i)
            output[written++] = static_cast<unsigned char>(decoded >> (i * 8));
        offset += count;
    }
    return written;
}

// Caller provides a live buffer of `count` bytes. No pointer escapes this call.
// Returning NEED_MORE on an invalid cursor avoids an out-of-bounds C++ read;
// these states are not reachable through the original ReadLine caller.
inline int NextChar(const unsigned char* buffer, size_t count, size_t& position, uint32_t encoding)
{
    if (position > count || (!buffer && count)) return -1;
    const size_t remaining = count - position;
    const uint32_t available = remaining < 4 ? static_cast<uint32_t>(remaining) : 4;
    uint32_t packed = 0;
    for (uint32_t i = 0; i < available; ++i)
        packed |= static_cast<uint32_t>(buffer[position + i]) << (8 * i);
    const uint64_t result = xy_vsfilter_next_char(packed, available, encoding);
    position += static_cast<uint32_t>(result >> 32);
    const uint32_t value = static_cast<uint32_t>(result);
    // Avoid implementation-defined unsigned-to-signed conversion of statuses.
    if (value == UINT32_MAX) return -1;
    if (value == UINT32_MAX - 1) return -2;
    return static_cast<int>(value);
}
}

