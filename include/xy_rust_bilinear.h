// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_BILINEAR_CDECL __cdecl
#else
#define XY_BILINEAR_CDECL
#endif
extern "C" void XY_BILINEAR_CDECL xy_vsfilter_bilinear_row(uint8_t*, uint16_t*, int32_t width, uint32_t x_factor, uint32_t y_factor);
#undef XY_BILINEAR_CDECL
