// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_BE_FILTER_CDECL __cdecl
#else
#define XY_BE_FILTER_CDECL
#endif
extern "C" void XY_BE_FILTER_CDECL xy_vsfilter_be_filter_row(uint8_t*, int32_t width, uint32_t policy);
#undef XY_BE_FILTER_CDECL
