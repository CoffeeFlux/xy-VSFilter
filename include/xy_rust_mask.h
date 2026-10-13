// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_MASK_CDECL __cdecl
#else
#define XY_MASK_CDECL
#endif
extern "C" uint8_t XY_MASK_CDECL xy_vsfilter_mask_scalar(uint8_t source, uint8_t body, uint8_t mask, int32_t color_alpha, uint32_t flags);
#undef XY_MASK_CDECL
