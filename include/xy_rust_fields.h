// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <cwchar>
#include <vector>

#ifdef _MSC_VER
#define XY_FIELDS_CDECL __cdecl
#else
#define XY_FIELDS_CDECL
#endif
extern "C" uint64_t XY_FIELDS_CDECL xy_vsfilter_next_field(const uint16_t*, uint32_t, uint16_t);
#undef XY_FIELDS_CDECL

namespace XyRust {
// The caller owns a mutable, NUL-terminated native string. Keep its storage and
// pointer mutations native; Rust borrows a bounded copy and returns offsets.
inline const wchar_t* NextField(wchar_t** cursor, wchar_t separator) {
    wchar_t* input = *cursor;
    const size_t length = std::wcslen(input);
    if (length > UINT32_MAX) abort();
    std::vector<uint16_t> units(length);
    for (size_t i=0; i<length; ++i) units[i] = static_cast<uint16_t>(input[i]);
    const uint64_t result = xy_vsfilter_next_field(units.data(), static_cast<uint32_t>(length), static_cast<uint16_t>(separator));
    const size_t start = static_cast<uint32_t>(result), end = static_cast<uint32_t>(result >> 32);
    if (start > end || end > length) abort();
    *cursor = input + (input[end] ? end + 1 : start);
    input[end] = 0;
    return input + start;
}
}
