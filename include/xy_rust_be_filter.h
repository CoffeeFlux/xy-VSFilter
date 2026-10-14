// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_BE_FILTER_CDECL __cdecl
#else
#define XY_BE_FILTER_CDECL
#endif
extern "C" void XY_BE_FILTER_CDECL xy_vsfilter_be_filter_row(uint8_t*, int32_t width, uint32_t policy);
extern "C" uint8_t XY_BE_FILTER_CDECL xy_vsfilter_weighted_pixel(uint32_t neighbors, uint32_t center, uint32_t side, uint32_t middle, uint32_t policy);
extern "C" uint64_t XY_BE_FILTER_CDECL xy_vsfilter_weighted_group(uint64_t next, uint32_t state, uint32_t side, uint32_t middle, uint32_t policy);
#undef XY_BE_FILTER_CDECL
