// SPDX-License-Identifier: GPL-2.0-or-later
// Real ATL strings on Windows. The portable storage stand-in is checked by the
// Windows oracle; it is not independent evidence of undocumented ATL behavior.
#pragma once
#include <string>
#include <vector>
#include <cwchar>
#include "xy_rust_drawing_tokens.h"
#ifdef _WIN32
#include <atlstr.h>
typedef ATL::CStringW DrawingString;
#else
typedef wchar_t WCHAR;
class DrawingString {
    std::wstring value;
public:
    DrawingString() {}
    DrawingString(const wchar_t* input, int length) : value(input, static_cast<size_t>(length)) {}
    int GetLength() const { return static_cast<int>(value.size()); }
    wchar_t operator[](size_t index) const { return value.at(index); }
    bool IsEmpty() const { return value.empty(); }
    DrawingString Tokenize(const wchar_t* separators, int& cursor) const {
        if (cursor >= 0 && static_cast<size_t>(cursor) < value.size()) {
            size_t start=static_cast<size_t>(cursor);
            while (start<value.size() && value[start] && std::wcschr(separators,value[start])) ++start;
            if (start<value.size()) {
                size_t end=start;
                while (end<value.size() && value[end] && !std::wcschr(separators,value[end])) ++end;
                cursor=static_cast<int>(end+1);
                return DrawingString(value.data()+start,static_cast<int>(end-start));
            }
        }
        cursor=-1; return DrawingString();
    }
    void TrimLeft(const wchar_t* characters) {
        size_t n=0;
        while (n<value.size() && value[n] && std::wcschr(characters,value[n])) ++n;
        value.erase(0,n);
    }
};
#endif
struct LexedDrawingToken {
    uint16_t command;
    std::vector<uint16_t> parameters;
    LexedDrawingToken(WCHAR c, const DrawingString& s) : command(static_cast<uint16_t>(c)) {
        for (int i=0; i<s.GetLength(); ++i) parameters.push_back(static_cast<uint16_t>(s[i]));
    }
    bool operator==(const LexedDrawingToken& other) const { return command==other.command && parameters==other.parameters; }
};
typedef std::vector<LexedDrawingToken> DrawingTokenList;
#define CStringW DrawingString
