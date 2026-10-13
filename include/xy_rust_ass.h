// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <vector>
#include <cwctype>
#include <new>

// Private to the native library; these symbols are not public DLL exports.
#ifdef _MSC_VER
#define XY_ASS_CDECL __cdecl
#else
#define XY_ASS_CDECL
#endif
extern "C" void* XY_ASS_CDECL xy_vsfilter_ass_scan(const uint16_t*, const uint8_t*, uint32_t);
extern "C" uint64_t XY_ASS_CDECL xy_vsfilter_ass_field(const void*, uint32_t, uint32_t, uint32_t);
extern "C" void XY_ASS_CDECL xy_vsfilter_ass_release(void*);
#ifdef _WIN32
extern "C" void XY_ASS_CDECL xy_vsfilter_native_runtime_v1();
#endif
#undef XY_ASS_CDECL

namespace XyRust {
class AssTokens {
    void* owner;
    AssTokens(const AssTokens&) = delete;
    AssTokens& operator=(const AssTokens&) = delete;
public:
    struct Span { int start, length; };
    // Input is the live CString storage, with its explicit nonnegative length.
    // Copying avoids wchar_t representation/aliasing assumptions. Whitespace
    // classification stays with the same native CRT and locale as the original.
    template<typename Unit> AssTokens(const Unit* input, int length) : owner(nullptr) {
#ifdef _WIN32
        xy_vsfilter_native_runtime_v1(); // Fail linking if built with Rust std.
#endif
        if (length < 0 || (!input && length)) throw std::bad_alloc();
        std::vector<uint16_t> units(length);
        std::vector<uint8_t> whitespace(length);
        for (int i = 0; i < length; ++i) {
            units[i] = static_cast<uint16_t>(input[i]);
            whitespace[i] = iswspace(input[i]) ? 1 : 0;
        }
        owner = xy_vsfilter_ass_scan(units.data(), whitespace.data(), static_cast<uint32_t>(length));
        if (!owner) throw std::bad_alloc();
    }
    ~AssTokens() { xy_vsfilter_ass_release(owner); }
    uint32_t size() const { return static_cast<uint32_t>(xy_vsfilter_ass_field(owner, 0, 0, 0)); }
    uint32_t parameters(uint32_t tag) const {
        return static_cast<uint32_t>(xy_vsfilter_ass_field(owner, tag, 2, 0));
    }
    Span span(uint32_t tag, uint32_t field, uint32_t parameter = 0) const {
        const uint64_t packed = xy_vsfilter_ass_field(owner, tag, field, parameter);
        // Each span lies within the input's nonnegative int length.
        return {static_cast<int>(static_cast<uint32_t>(packed)), static_cast<int>(packed >> 32)};
    }
};
}
