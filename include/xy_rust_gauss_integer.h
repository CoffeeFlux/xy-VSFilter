// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_GAUSS_INTEGER_CDECL __cdecl
#else
#define XY_GAUSS_INTEGER_CDECL
#endif
extern "C" uint32_t XY_GAUSS_INTEGER_CDECL xy_vsfilter_gauss_integer(uint8_t* buffer, uint32_t* scratch, int32_t width, int32_t height, int32_t stride, const uint32_t* x_table, int32_t x_radius, int32_t x_width, const uint32_t* y_table, int32_t y_radius, int32_t y_width);
#undef XY_GAUSS_INTEGER_CDECL
