// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

#ifdef _MSC_VER
#define XY_READER_CDECL __cdecl
#else
#define XY_READER_CDECL
#endif
extern "C" {
void* XY_READER_CDECL xy_vsfilter_reader_create();
uint64_t XY_READER_CDECL xy_vsfilter_reader_step(void*, uint32_t, uint32_t);
size_t XY_READER_CDECL xy_vsfilter_reader_field(const void*, uint32_t, size_t);
uint32_t XY_READER_CDECL xy_vsfilter_reader_capacity(void*);
unsigned char* XY_READER_CDECL xy_vsfilter_reader_destination(void*);
void XY_READER_CDECL xy_vsfilter_reader_release(void*);
}
#undef XY_READER_CDECL

namespace XyRust {
// Internal noncopyable owner, never exposed through a public DLL interface.
class TextBuffer {
    void* owner;
    TextBuffer(const TextBuffer&);
    TextBuffer& operator=(const TextBuffer&);
public:
    enum { Read = 0, Line = 1, Reopen = 2, Log = 3 };
    TextBuffer() : owner(xy_vsfilter_reader_create()) { if (!owner) abort(); }
    ~TextBuffer() { xy_vsfilter_reader_release(owner); }
    void ClearLine() { xy_vsfilter_reader_step(owner, 0, 0); }
    void Invalidate() { xy_vsfilter_reader_step(owner, 1, 0); }
    void AppendReplacement() { xy_vsfilter_reader_step(owner, 2, 0); }
    uint64_t Advance(uint32_t encoding, bool bom) {
        return xy_vsfilter_reader_step(owner, 4, encoding | (bom ? 0x100 : 0));
    }
    int NextChar(uint32_t encoding) {
        const uint32_t c = static_cast<uint32_t>(xy_vsfilter_reader_step(owner, 5, encoding));
        return c == UINT32_MAX ? -1 : c == UINT32_MAX - 1 ? -2 : static_cast<int>(c);
    }
    size_t Position() const { return xy_vsfilter_reader_field(owner, 0, 0); }
    size_t Count() const { return xy_vsfilter_reader_field(owner, 1, 0); }
    size_t Unread() const { const size_t count = Count(); return count ? count - Position() : 0; }
    uint32_t ReadCapacity() { return xy_vsfilter_reader_capacity(owner); }
    // Query capacity first; borrow this pointer ONLY for the native Read call.
    // No Rust operation on this owner is permitted until Read returns/throws.
    unsigned char* ReadDestination() { return xy_vsfilter_reader_destination(owner); }
    void FinishRead(uint32_t count) {
        if (!xy_vsfilter_reader_step(owner, 3, count)) abort();
    }
    template<typename Unit> void CopyLine(std::vector<Unit>& output) const {
        const size_t length = xy_vsfilter_reader_field(owner, 2, 0);
        output.clear();
        output.reserve(length);
        for (size_t i = 0; i < length; ++i)
            output.push_back(static_cast<Unit>(xy_vsfilter_reader_field(owner, 3, i)));
    }
};
}
