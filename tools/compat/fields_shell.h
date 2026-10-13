// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xy_rust_fields.h"
#include <string>
#ifdef _WIN32
#include <atlstr.h>
typedef ATL::CStringW FieldString;
#else
typedef wchar_t WCHAR;
class FieldString {
    std::wstring value;
public:
    typedef const wchar_t* PCXSTR;
    typedef wchar_t* PXSTR;
    FieldString() {}
    FieldString(const wchar_t* input, int length) : value(input, static_cast<size_t>(length)) {}
    int GetLength() const { return static_cast<int>(value.size()); }
    wchar_t operator[](int index) const { return value.at(static_cast<size_t>(index)); }
    void TrimLeft() {
        size_t n=0;
        while (n<value.size() && value[n] && std::iswspace(value[n])) ++n;
        value.erase(0,n);
    }
    int Find(wchar_t separator) const {
        if (value.empty()) return -1;
        const wchar_t* found = std::wcschr(value.c_str(),separator);
        return found ? static_cast<int>(found-value.c_str()) : -1;
    }
    FieldString Left(int count) const { return FieldString(value.data(),count); }
    void Delete(int start,int count) { value.erase(static_cast<size_t>(start),static_cast<size_t>(count)); }
};
#endif
#define CStringW FieldString
