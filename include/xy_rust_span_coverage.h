// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_COVERAGE_CDECL __cdecl
#else
#define XY_COVERAGE_CDECL
#endif
extern "C" uint32_t XY_COVERAGE_CDECL xy_vsfilter_span_coverage(uint8_t*,size_t,int32_t,int32_t,int32_t,int32_t);
#undef XY_COVERAGE_CDECL
namespace XyRust {
inline bool AccumulateSpan(uint8_t* plane,int pitch,int height,int y,int x1,int x2){
    if(x2<=x1)return true;
    if(!plane || pitch<=0 || height<=0)return false;
    const int64_t count=static_cast<int64_t>(pitch)*height;
    // Rasterize allocated this plane using the same native signed-int product.
    if(count>INT32_MAX || static_cast<uint64_t>(count)>static_cast<uint64_t>(PTRDIFF_MAX))return false;
    return xy_vsfilter_span_coverage(plane,static_cast<size_t>(count),pitch,y,x1,x2)!=0;
}
}
