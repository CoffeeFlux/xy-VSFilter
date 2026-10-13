// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_FILL_CDECL __cdecl
#else
#define XY_FILL_CDECL
#endif
extern "C" {
void XY_FILL_CDECL xy_vsfilter_fill_masked16(uint8_t*, uint64_t low, uint64_t high, uint32_t color);
void XY_FILL_CDECL xy_vsfilter_fill_packed(uint8_t*, int32_t width, uint8_t alpha, uint32_t color);
void XY_FILL_CDECL xy_vsfilter_fill_plane(uint8_t*, int32_t width, uint8_t alpha, uint8_t value);
}
#undef XY_FILL_CDECL
