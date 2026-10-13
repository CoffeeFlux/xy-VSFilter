// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _MSC_VER
#define XY_SEGMENTS_CDECL __cdecl
#else
#define XY_SEGMENTS_CDECL
#endif
extern "C" {
void* XY_SEGMENTS_CDECL xy_vsfilter_segments_create();
void XY_SEGMENTS_CDECL xy_vsfilter_segments_add(void*, int64_t, int64_t);
size_t XY_SEGMENTS_CDECL xy_vsfilter_segments_build(void*);
uint64_t XY_SEGMENTS_CDECL xy_vsfilter_segments_field(const void*, size_t, uint32_t, size_t);
void XY_SEGMENTS_CDECL xy_vsfilter_segments_release(void*);
}
#undef XY_SEGMENTS_CDECL

namespace XyRust {
class Segments {
    void* owner;
    Segments(const Segments&);
    Segments& operator=(const Segments&);
    static int64_t Signed(uint64_t value) {
        return value <= INT64_MAX ? static_cast<int64_t>(value) :
            -static_cast<int64_t>(UINT64_MAX - value) - 1;
    }
public:
    Segments() : owner(xy_vsfilter_segments_create()) { if (!owner) abort(); }
    ~Segments() { xy_vsfilter_segments_release(owner); }
    void Add(int64_t start, int64_t end) { xy_vsfilter_segments_add(owner, start, end); }
    size_t Build() { return xy_vsfilter_segments_build(owner); }
    int64_t Start(size_t segment) const { return Signed(xy_vsfilter_segments_field(owner, segment, 0, 0)); }
    int64_t End(size_t segment) const { return Signed(xy_vsfilter_segments_field(owner, segment, 1, 0)); }
    size_t Count(size_t segment) const { return static_cast<size_t>(xy_vsfilter_segments_field(owner, segment, 2, 0)); }
    int Entry(size_t segment, size_t entry) const {
        const uint64_t index = xy_vsfilter_segments_field(owner, segment, 3, entry);
        if (index > INT32_MAX) abort();
        return static_cast<int>(index);
    }
};
}
