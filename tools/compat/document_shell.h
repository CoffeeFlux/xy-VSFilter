// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Portable storage/value shells approximate ATL; this is not Windows integration.
#include <algorithm>
#include <cmath>
#define TIME 0
#define FRAME 1
#define UNITS_FLOAT 10000000.0
#define TRACE_SUB(message) ((void)0)
#include <cstdint>
#include <climits>
#include <iostream>
#include <vector>
using REFERENCE_TIME = int64_t;
using std::min;
using std::max;
#ifndef _I64_MAX
#define _I64_MAX INT64_MAX
#endif
#define EXTVTT 1
template<class T> class CAtlArray : public std::vector<T> {
public:
 size_t GetCount() const { return this->size(); }
 bool IsEmpty() const { return this->empty(); }
 void RemoveAll() { this->clear(); }
 size_t Add(const T& v) { this->push_back(v); return this->size()-1; }
 void InsertAt(size_t i,const T& v) { this->insert(this->begin()+i,v); }
 T* GetData() { return this->data(); }
 T& GetAt(size_t i) { return this->at(i); }
 void Copy(const CAtlArray& a) { *this=a; }
};
struct STSEntry {
 std::vector<uint16_t> str,style,actor,effect;
 bool fUnicode;
 int marginRect[4],layer;
 REFERENCE_TIME start,end;
 int readorder;
};
