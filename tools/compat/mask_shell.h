// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include "xy_rust_mask.h"
typedef uint8_t BYTE;
// Portable builds exercise the original Windows x64 scalar delegation.
// Windows x86 executes the actual MMX/SSE bodies; do not emulate them here.
#if !defined(_WIN32) && !defined(_WIN64)
#define _WIN64
#define XY_MASK_PORTABLE_WIN64 1
#endif
#ifndef _WIN64
#include <mmintrin.h>
#include <emmintrin.h>
#endif
#include <cstring>
