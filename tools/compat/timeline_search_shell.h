// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include "xy_rust_timeline_search.h"
#define TRACE_SUB(message) ((void)0)
#define UNITS_FLOAT 10000000.0
#define TIME 0
#define FRAME 1

#define TIMELINE_SEARCH_SHELL(Name) \
class Name { \
public: \
    CAtlArray<STSEntry> m_entries; \
    CAtlArray<STSSegment> m_segments; \
    int m_mode=TIME, failAt=-1; \
    int* observedIndex=nullptr; \
    int* observedCount=nullptr; \
    std::vector<std::vector<int> > requests; \
    void Request(int kind, int index) { \
        requests.push_back({kind, index, observedIndex ? *observedIndex : -99, observedCount ? *observedCount : -99}); \
        if (static_cast<int>(requests.size())==failAt) throw 7; \
    } \
    REFERENCE_TIME TranslateStart(int i, double fps) { Request(0,i); return TranslateStartImpl(i,fps); } \
    REFERENCE_TIME TranslateSegmentStart(int i, double fps) { Request(1,i); return TranslateSegmentStartImpl(i,fps); } \
    REFERENCE_TIME TranslateSegmentEnd(int i, double fps) { Request(2,i); return TranslateSegmentEndImpl(i,fps); } \
    REFERENCE_TIME TranslateStartImpl(int i, double fps); \
    REFERENCE_TIME TranslateSegmentStartImpl(int i, double fps); \
    REFERENCE_TIME TranslateSegmentEndImpl(int i, double fps); \
    int SearchSub(REFERENCE_TIME t, double fps); \
    const STSSegment* SearchSubs(REFERENCE_TIME t, double fps, int* iSegment, int* nSegments); \
    STSSegment* SearchSubs2(REFERENCE_TIME t, double fps, int* iSegment, int* nSegments); \
};
