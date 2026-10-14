// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Portable storage/value shells approximate ATL; this is not Windows integration.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <climits>
#include <iostream>
#include <vector>
#include <memory>
#include <type_traits>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_NO_AUTOMATIC_NAMESPACE
#endif
#include <atlbase.h>
#include <atlstr.h>
#endif
#define TIME 0
#define FRAME 1
#define UNITS 10000000
#define UNITS_FLOAT 10000000.0
#define TRACE_SUB(message) ((void)0)

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
// Immutable arena values make record relocation defined for qsort. This models
// raw code units, not CString object layout, refcounts or native allocation.
// Replacing a field obtains a new handle, so saved records remain independent.
struct UnitString {
 const std::vector<uint16_t>* value=nullptr;
 static const std::vector<uint16_t>* store(const std::vector<uint16_t>& units) {
  static std::vector<std::unique_ptr<const std::vector<uint16_t>>> arena;
  arena.push_back(std::make_unique<const std::vector<uint16_t>>(units));
  return arena.back().get();
 }
 UnitString& operator=(const std::vector<uint16_t>& units) {value=store(units);return *this;}
 UnitString& operator=(std::initializer_list<uint16_t> units) {return *this=std::vector<uint16_t>(units);}
 operator const std::vector<uint16_t>&() const {return *value;}
 bool operator==(const std::vector<uint16_t>& units) const {
#ifdef _WIN32
  // Bind the extracted style comparison to actual Windows CStringW. Bounded
  // construction preserves embedded NUL and unpaired UTF-16 before equality.
  if(value->size()>INT_MAX||units.size()>INT_MAX)throw std::length_error("CString shell count");
  std::vector<wchar_t> left_units(value->begin(),value->end()),right_units(units.begin(),units.end());
  ATL::CStringW left,right;
  if(!left_units.empty())left.SetString(left_units.data(),static_cast<int>(left_units.size()));
  if(!right_units.empty())right.SetString(right_units.data(),static_cast<int>(right_units.size()));
  return left==right;
#else
  // Portable approximation only; Windows full-sequence evidence is required.
  const auto left_end=std::find(value->begin(),value->end(),uint16_t(0));
  const auto right_end=std::find(units.begin(),units.end(),uint16_t(0));
  return std::equal(value->begin(),left_end,units.begin(),right_end);
#endif
 }
 void clear() {*this=std::vector<uint16_t>();}
};
static_assert(std::is_trivially_copyable<UnitString>::value,"qsort string shell must be trivially copyable");
struct STSEntry {
 UnitString str;
 bool fUnicode;
 UnitString style,actor,effect;
 int marginRect[4],layer;
 REFERENCE_TIME start,end;
 int readorder;
};

static_assert(std::is_trivially_copyable<STSEntry>::value,"qsort record shell must be trivially copyable");
