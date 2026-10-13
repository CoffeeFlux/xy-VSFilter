// SPDX-License-Identifier: GPL-2.0-or-later
// Storage-only stand-ins. The scanner and its helpers are extracted verbatim.
#pragma once
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

const int CMD_COUNT = 0;
#ifdef _WIN32
#include <atlstr.h>
// Use the real ATL string on Windows, including its embedded-NUL semantics.
class OracleString : public ATL::CStringW {
public:
    explicit OracleString(const std::wstring& input)
        : ATL::CStringW(input.data(), static_cast<int>(input.size())) {}
    OracleString(const wchar_t* input, size_t length)
        : ATL::CStringW(input, static_cast<int>(length)) {}
    OracleString() {}
};
#else
typedef wchar_t WCHAR;
class OracleString {
    std::wstring value;
public:
    typedef const wchar_t* PCXSTR;
    explicit OracleString(const std::wstring& input) : value(input) {}
    OracleString(const wchar_t* input, size_t length) : value(input, length) {}
    OracleString() {}
    int GetLength() const { return static_cast<int>(value.size()); }
    const wchar_t* GetString() const { return value.c_str(); }
    int Find(wchar_t c, int start) const {
        if (start < 0 || static_cast<size_t>(start) >= value.size()) return -1;
        const wchar_t* found = std::wcschr(value.c_str() + start, c);
        return found ? static_cast<int>(found - value.c_str()) : -1;
    }
    bool IsEmpty() const { return value.empty(); }
    wchar_t operator[](size_t index) const { return index == value.size() ? 0 : value.at(index); }
    bool operator==(const OracleString& other) const { return value == other.value; }
};
#endif
template<typename T> struct OracleArray : std::vector<T> {
    void Add(const T& value) { this->push_back(value); }
};
struct AssTag {
    int cmdType;
    OracleString command;
    OracleArray<OracleString> strParams;
    bool operator==(const AssTag& other) const {
        return cmdType == other.cmdType && command == other.command && strParams == other.strParams;
    }
};
struct AssTagList : std::vector<AssTag> {
    size_t AddTail() { this->emplace_back(); return this->size() - 1; }
    AssTag& GetAt(size_t pos) { return this->at(pos); }
};
// Substitute only the storage types around the unmodified source block.
#define CStringW OracleString
#define CAtlArray OracleArray
#define POSITION size_t
