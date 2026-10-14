// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_SSF_SPLIT_CDECL __cdecl
#else
#define XY_SSF_SPLIT_CDECL
#endif
extern "C" int32_t XY_SSF_SPLIT_CDECL xy_vsfilter_ssf_split_find(const uint16_t*,int32_t,const uint16_t*,size_t,int32_t);
#undef XY_SSF_SPLIT_CDECL
