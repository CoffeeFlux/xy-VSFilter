// SPDX-License-Identifier: GPL-2.0-or-later
// Stand-ins only at the unchanged CString/GetPOINT and array-storage boundary.
// Both extracted implementations receive identical command tokens and point
// results. Full Windows DLL comparisons exercise real tokenization and wcstod.
#pragma once
#include <cstdint>
#include <vector>
#include "xy_rust_drawing.h"

typedef unsigned char BYTE;
typedef wchar_t WCHAR;
enum { PT_MOVETO=6, PT_MOVETONC=0xfe, PT_LINETO=2, PT_BEZIERTO=4,
       PT_BSPLINETO=0xfc, PT_BSPLINEPATCHTO=0xfa };
struct CPoint {
    int32_t x, y;
    CPoint(int32_t a=0, int32_t b=0) : x(a), y(b) {}
    bool operator==(const CPoint& p) const { return x==p.x && y==p.y; }
};
struct PointResult { bool success; CPoint point; };
struct DrawingToken { WCHAR command; std::vector<PointResult> points; };
class CStringW {
public:
    std::vector<DrawingToken> tokens;
    DrawingToken token;
    size_t point;
    bool empty;
    CStringW() : point(0), empty(true) {}
    explicit CStringW(const std::vector<DrawingToken>& input) : tokens(input), point(0), empty(false) {}
    explicit CStringW(const DrawingToken& input) : token(input), point(0), empty(false) {}
    CStringW Get() const { return *this; }
    void SpanIncluding(const wchar_t*) {}
    void Replace(const wchar_t*, const wchar_t*) {}
    void TrimLeft(const wchar_t*) {}
    CStringW Tokenize(const wchar_t*, int& k) const {
        return static_cast<size_t>(k) < tokens.size() ? CStringW(tokens[k++]) : CStringW();
    }
    bool IsEmpty() const { return empty; }
    WCHAR operator[](int) const { return token.command; }
};
template<typename T> struct DrawingArray : std::vector<T> {
    size_t GetCount() const { return this->size(); }
    void Add(const T& item) { this->push_back(item); }
    void SetCount(size_t count) { this->resize(count); }
    void RemoveAll() { this->clear(); }
};

#define DRAWING_SHELL(Name) \
class Name { \
public: \
    CStringW m_str; \
    DrawingArray<BYTE> m_pathTypesOrg; \
    DrawingArray<CPoint> m_pathPointsOrg; \
    std::vector<uint64_t> requests; \
    explicit Name(const std::vector<DrawingToken>& tokens) : m_str(tokens) {} \
    bool GetPOINT(CStringW& token, CPoint& p) { \
        requests.push_back((static_cast<uint64_t>(token.token.command) << 32) | token.point); \
        if (token.point >= token.token.points.size()) return false; \
        const PointResult& result = token.token.points[token.point++]; \
        if (result.success) p = result.point; \
        return result.success; \
    } \
    bool ParseStr(); \
};
