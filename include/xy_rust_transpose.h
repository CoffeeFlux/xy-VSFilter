// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_TRANSPOSE_CDECL __cdecl
#else
#define XY_TRANSPOSE_CDECL
#endif
extern "C" uint32_t XY_TRANSPOSE_CDECL xy_vsfilter_byte_transpose(uint8_t* destination, int32_t destination_width, int32_t destination_stride, const uint8_t* source, int32_t width, int32_t height, int32_t source_stride);
extern "C" uint32_t XY_TRANSPOSE_CDECL xy_vsfilter_word_transpose(uint32_t* destination, int32_t destination_width, int32_t destination_stride, const uint32_t* source, int32_t width, int32_t height, int32_t source_stride);
#undef XY_TRANSPOSE_CDECL
