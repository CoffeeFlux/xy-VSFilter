// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include "xy_rust_be_filter.h"
typedef uint8_t UINT8, *PUINT8;
#define ASSERT(condition) ((void)0)
#ifndef _MSC_VER
#define __forceinline inline
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#define FILTER_NATIVE_SSE 1
#include <emmintrin.h>
#else
#define FILTER_NATIVE_SSE 0
#endif
