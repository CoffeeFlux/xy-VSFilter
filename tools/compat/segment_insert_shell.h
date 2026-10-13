// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xy_rust_segment_insert.h"
#include <functional>
#include <type_traits>
#include <climits>

static std::function<void(int,int,size_t)> insertionObserver;
static int insertionDepth=0;
// Observe/inject only at the outer call boundary. A throw inside ATL relocation
// may leave reference storage uninitialized; that is not a passing test target.
struct InsertionCall {
    InsertionCall(int kind,int op,size_t position) {
        if (!insertionDepth && insertionObserver) insertionObserver(kind,op,position);
        ++insertionDepth;
    }
    ~InsertionCall() { --insertionDepth; }
};
template<typename T> class ObservedArray : public CAtlArray<T> {
    enum { kind=std::is_same<T,int>::value ? 0 : 1 };
public:
    size_t Add(const T& value) { InsertionCall call(kind,1,this->GetCount()); return CAtlArray<T>::Add(value); }
    void InsertAt(size_t at,const T& value) { InsertionCall call(kind,2,at); CAtlArray<T>::InsertAt(at,value); }
    void Copy(const ObservedArray& other) { InsertionCall call(kind,0,other.GetCount()); CAtlArray<T>::Copy(other); }
};
struct PreparedEntry { REFERENCE_TIME start,end; int readorder; };
#ifndef _I64_MAX
#define _I64_MAX INT64_MAX
#endif
using std::min;
using std::max;
#define EXTVTT 1
#define INSERTION_SHELL(Name) \
class Name { \
public: \
    CAtlArray<PreparedEntry> m_entries; \
    ObservedArray<InsertionSegment> m_segments; \
    int m_subtitleType=0; \
    void AddPrepared(REFERENCE_TIME start,REFERENCE_TIME end,int readorder); \
};
