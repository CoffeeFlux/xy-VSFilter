// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <list>
#include "xy_rust_realtext.h"
static std::vector<int64_t> realTextConversions;
static int realTextDigit(wint_t c){const int result=::iswdigit(c);realTextConversions.insert(realTextConversions.end(),{1,int64_t(c),result});return result;}
static int realTextToInt(const wchar_t* text){
#ifdef _WIN32
    const int result=::_wtoi(text);
#else
    const int result=static_cast<int>(std::wcstol(text,nullptr,10));
#endif
    realTextConversions.push_back(2);realTextConversions.push_back(result);
    while(*text)realTextConversions.push_back(*text++);realTextConversions.push_back(0);return result;
}
#define iswdigit realTextDigit
#define _wtoi realTextToInt
