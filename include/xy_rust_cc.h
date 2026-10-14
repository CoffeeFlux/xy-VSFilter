// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_CC_CDECL __cdecl
#else
#define XY_CC_CDECL
#endif
extern "C" {
uint32_t XY_CC_CDECL xy_vsfilter_cc_move(int32_t, int32_t, int32_t*);
uint32_t XY_CC_CDECL xy_vsfilter_cc_put(uint16_t*, int32_t, int32_t, uint16_t);
}
#undef XY_CC_CDECL
