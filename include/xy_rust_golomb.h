// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_GOLOMB_CDECL __cdecl
#else
#define XY_GOLOMB_CDECL
#endif
struct XyGolombStep {
    uint64_t buffer;
    uint64_t value;
    int32_t length;
    uint32_t action; // 0=done, 1=read native byte, 2=retain original wider path
};
extern "C" XyGolombStep XY_GOLOMB_CDECL xy_vsfilter_golomb_step(
    uint64_t buffer, int32_t length, int32_t position, int32_t size, int32_t bits, uint32_t peek);
#undef XY_GOLOMB_CDECL
