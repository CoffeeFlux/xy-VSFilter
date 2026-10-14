// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_SSF_ARABIC_CDECL __cdecl
#else
#define XY_SSF_ARABIC_CDECL
#endif
extern "C" {
uint32_t XY_SSF_ARABIC_CDECL xy_vsfilter_ssf_is_arabic(uint16_t);
uint32_t XY_SSF_ARABIC_CDECL xy_vsfilter_ssf_arabic_form(uint16_t,uint32_t);
uint32_t XY_SSF_ARABIC_CDECL xy_vsfilter_ssf_arabic_neighbors(uint16_t,uint16_t,uint16_t);
}
#undef XY_SSF_ARABIC_CDECL
