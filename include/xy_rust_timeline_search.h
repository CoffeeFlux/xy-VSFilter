// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>

#ifdef _MSC_VER
#define XY_SEARCH_CDECL __cdecl
#else
#define XY_SEARCH_CDECL
#endif
extern "C" {
void* XY_SEARCH_CDECL xy_vsfilter_search_create(uint32_t, int64_t, int32_t);
void XY_SEARCH_CDECL xy_vsfilter_search_supply(void*, int64_t);
int32_t XY_SEARCH_CDECL xy_vsfilter_search_field(const void*, uint32_t);
void XY_SEARCH_CDECL xy_vsfilter_search_release(void*);
}
#undef XY_SEARCH_CDECL

namespace XyRust {
class TimelineSearch {
    void* owner;
    TimelineSearch(const TimelineSearch&);
    TimelineSearch& operator=(const TimelineSearch&);
public:
    TimelineSearch(uint32_t mode, int64_t time, int32_t count)
        : owner(xy_vsfilter_search_create(mode, time, count)) { if (!owner) abort(); }
    ~TimelineSearch() { xy_vsfilter_search_release(owner); }
    bool Pending() const { return xy_vsfilter_search_field(owner, 0) != 0; }
    int Index() const { return xy_vsfilter_search_field(owner, 1); }
    bool Start() const { return xy_vsfilter_search_field(owner, 2) != 0; }
    void Supply(int64_t time) { xy_vsfilter_search_supply(owner, time); }
    int Result() const { return xy_vsfilter_search_field(owner, 3); }
    bool Found() const { return xy_vsfilter_search_field(owner, 4) != 0; }
};
}
