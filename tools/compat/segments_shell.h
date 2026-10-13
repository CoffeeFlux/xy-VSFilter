// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "xy_rust_segments.h"
typedef int64_t REFERENCE_TIME;
#ifdef _WIN32
#include <atlbase.h>
#include <atlcoll.h>
#else
// Only storage is substituted off Windows. The real ATL array and the original
// STSSegment class are used on Windows; both method bodies are extracted intact.
template<typename T> class CAtlArray : public std::vector<T> {
public:
    size_t GetCount() const { return this->size(); }
    void RemoveAll() { this->clear(); }
    void Add(const T& item) { this->push_back(item); }
    T& GetAt(size_t index) { return this->at(index); }
    void Copy(const CAtlArray& other) { *this = other; }
};
#endif

struct STSEntry { REFERENCE_TIME start, end; };
// Native callbacks stay outside Rust. Record their order and the full segment
// contents they can observe, not merely the final array after the method returns.
template<typename Array> std::vector<int64_t> SegmentSnapshot(const Array& segments) {
    std::vector<int64_t> result;
    result.push_back(static_cast<int64_t>(segments.GetCount()));
    for (size_t i=0; i<segments.GetCount(); ++i) {
        const auto& s=segments[i];
        result.push_back(s.start); result.push_back(s.end); result.push_back(s.animated);
        result.push_back(static_cast<int64_t>(s.subs.GetCount()));
        for (size_t j=0; j<s.subs.GetCount(); ++j) result.push_back(s.subs[j]);
    }
    return result;
}
#define SEGMENTS_SHELL(Name) \
class Name { \
public: \
    CAtlArray<STSEntry> m_entries; \
    CAtlArray<STSSegment> m_segments; \
    std::vector<std::vector<int64_t> > callbacks; \
    void OnChanged() { callbacks.push_back({1}); callbacks.push_back(SegmentSnapshot(m_segments)); } \
    void LogSegments(const CAtlArray<STSSegment>& segments) { callbacks.push_back({2}); callbacks.push_back(SegmentSnapshot(segments)); } \
    void CreateSegments(); \
};
