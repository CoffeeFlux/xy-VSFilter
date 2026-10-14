// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_MASK_CDECL __cdecl
#else
#define XY_MASK_CDECL
#endif
struct XYRustMaskBytes { uint64_t low, high; };
extern "C" XYRustMaskBytes XY_MASK_CDECL xy_vsfilter_mask_block(XYRustMaskBytes source, XYRustMaskBytes body, XYRustMaskBytes mask, int32_t color_alpha, uint32_t flags, uint32_t mmx);
extern "C" uint8_t XY_MASK_CDECL xy_vsfilter_mask_scalar(uint8_t source, uint8_t body, uint8_t mask, int32_t color_alpha, uint32_t flags);
extern "C" uint32_t XY_MASK_CDECL xy_vsfilter_clip_row(uint8_t* destination, const uint8_t* source, size_t count, uint32_t inverse);
#undef XY_MASK_CDECL
