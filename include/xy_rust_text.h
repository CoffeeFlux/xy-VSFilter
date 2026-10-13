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
#undef XY_RUST_CDECL

namespace XyRust {
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

