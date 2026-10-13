// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define XY_INSERT_CDECL __cdecl
#else
#define XY_INSERT_CDECL
#endif
extern "C" {
void* XY_INSERT_CDECL xy_vsfilter_insert_create();
uint32_t XY_INSERT_CDECL xy_vsfilter_insert_seed(void*, uint32_t, int64_t, int64_t);
uint32_t XY_INSERT_CDECL xy_vsfilter_insert_build(void*, int64_t, int64_t, size_t, size_t);
uint64_t XY_INSERT_CDECL xy_vsfilter_insert_field(const void*, size_t, uint32_t);
void XY_INSERT_CDECL xy_vsfilter_insert_release(void*);
}
#undef XY_INSERT_CDECL
namespace XyRust {
class SegmentInsertion {
    void* owner;
    SegmentInsertion(const SegmentInsertion&);
    SegmentInsertion& operator=(const SegmentInsertion&);
    static int64_t Signed(uint64_t value) {
        return value <= INT64_MAX ? static_cast<int64_t>(value) : -static_cast<int64_t>(UINT64_MAX-value)-1;
    }
    static int Entry(uint64_t value) { if (value > INT32_MAX) abort(); return static_cast<int>(value); }
    uint64_t Field(size_t edit, uint32_t field) const {
        return xy_vsfilter_insert_field(owner,edit,field);
    }
public:
    SegmentInsertion() : owner(xy_vsfilter_insert_create()) { if (!owner) abort(); }
    ~SegmentInsertion() { xy_vsfilter_insert_release(owner); }
    void Order(int32_t order) { if (!xy_vsfilter_insert_seed(owner,0,order,0)) abort(); }
    void Segment(int64_t start, int64_t end) { if (!xy_vsfilter_insert_seed(owner,1,start,end)) abort(); }
    void Sub(int entry) { if (!xy_vsfilter_insert_seed(owner,2,entry,0)) abort(); }
    void Build(int64_t start, int64_t end, int entry, size_t position) {
        if (entry < 0 || !xy_vsfilter_insert_build(owner,start,end,static_cast<size_t>(entry),position)) abort();
    }
    template<typename NativeSegment, typename Array> void Apply(Array& segments) const {
        const size_t count=static_cast<size_t>(Field(0,6));
        for (size_t edit=0; edit<count; ++edit) {
            const size_t at=static_cast<size_t>(Field(edit,1));
            const uint64_t kind=Field(edit,0);
            if (kind == 2 ? at > segments.GetCount() : at >= segments.GetCount()) abort();
            switch (kind) {
            case 0: {
                const int64_t start=Signed(Field(edit,2));
                NativeSegment segment(segments[at].start,start);
                segment.subs.Copy(segments[at].subs);
                segments[at].start=start;
                segments.InsertAt(at,segment);
                break;
            }
            case 1: {
                const int64_t end=Signed(Field(edit,2));
                NativeSegment segment(end,segments[at].end);
                segment.subs.Copy(segments[at].subs);
                segments[at].end=end;
                segments.InsertAt(at+1,segment);
                break;
            }
            case 2: {
                NativeSegment segment(Signed(Field(edit,2)),Signed(Field(edit,3)));
                segment.subs.Add(Entry(Field(edit,4)));
                segments.InsertAt(at,segment);
                break;
            }
            case 3: segments[at].subs.Add(Entry(Field(edit,2))); break;
            case 4: {
                const size_t position=static_cast<size_t>(Field(edit,3));
                if (position > segments[at].subs.GetCount()) abort();
                segments[at].subs.InsertAt(position,Entry(Field(edit,2)));
                break;
            }
            default: abort();
            }
        }
    }
};
}
