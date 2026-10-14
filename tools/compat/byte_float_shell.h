// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include "xy_rust_byte_float.h"
typedef uint8_t UINT8,*PUINT8;
typedef const UINT8* PCUINT8;
#define ASSERT(condition) ((void)0)
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#define BYTE_FLOAT_NATIVE_SSE 1
#include <emmintrin.h>
#else
#define BYTE_FLOAT_NATIVE_SSE 0
#endif
static uint64_t byteFloatHandled=0,byteFloatFallback=0;
static uint32_t observedByteFloat(uint32_t* dst,int32_t dstWidth,int32_t dstStride,const uint8_t* src,int32_t width,int32_t height,int32_t srcStride) {
    const uint32_t result=xy_vsfilter_byte_float(dst,dstWidth,dstStride,src,width,height,srcStride);
    byteFloatHandled+=result!=0;byteFloatFallback+=result==0;return result;
}
#define xy_vsfilter_byte_float observedByteFloat
