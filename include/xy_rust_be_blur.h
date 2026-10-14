// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_BE_BLUR_CDECL __cdecl
#else
#define XY_BE_BLUR_CDECL
#endif
struct XYRustBlurStep { uint64_t pixels; uint32_t state, reserved; };
extern "C" XYRustBlurStep XY_BE_BLUR_CDECL xy_vsfilter_be_blur_group(uint64_t source, uint32_t count, uint32_t state, uint16_t* column_pixels, uint16_t* column_sums, uint32_t phase, uint32_t vector);
#undef XY_BE_BLUR_CDECL
