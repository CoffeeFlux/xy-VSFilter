// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include "xy_rust_transpose.h"
typedef uint8_t UINT8, *PUINT8;
typedef const UINT8* PCUINT8;
#define ASSERT(condition) ((void)0)
static uint64_t transposeHandled=0,transposeFallback=0;
static uint32_t observedTranspose(uint8_t* dst,int32_t dstWidth,int32_t dstStride,const uint8_t* src,int32_t width,int32_t height,int32_t srcStride) {
    const uint32_t result=xy_vsfilter_byte_transpose(dst,dstWidth,dstStride,src,width,height,srcStride);
    transposeHandled+=result!=0;transposeFallback+=result==0;return result;
}
#define xy_vsfilter_byte_transpose observedTranspose
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#define TRANSPOSE_NATIVE_SSE 1
#include <xmmintrin.h>
#else
#define TRANSPOSE_NATIVE_SSE 0
#endif
static uint64_t wordTransposeHandled=0,wordTransposeFallback=0;
static uint32_t observedWordTranspose(uint32_t* dst,int32_t dstWidth,int32_t dstStride,const uint32_t* src,int32_t width,int32_t height,int32_t srcStride) {
    const uint32_t result=xy_vsfilter_word_transpose(dst,dstWidth,dstStride,src,width,height,srcStride);
    wordTransposeHandled+=result!=0;wordTransposeFallback+=result==0;return result;
}
#define xy_vsfilter_word_transpose observedWordTranspose
