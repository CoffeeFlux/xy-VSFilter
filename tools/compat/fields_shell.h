// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xy_rust_fields.h"
typedef wchar_t WCHAR;
struct FieldString {
    typedef const wchar_t* PCXSTR;
    typedef wchar_t* PXSTR;
};
#define CStringW FieldString
