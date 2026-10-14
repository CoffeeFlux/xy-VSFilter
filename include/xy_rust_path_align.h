// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_PATH_ALIGN_CDECL __cdecl
#else
#define XY_PATH_ALIGN_CDECL
#endif
extern "C" uint32_t XY_PATH_ALIGN_CDECL xy_vsfilter_path_align(void*,int32_t,int32_t*);
#undef XY_PATH_ALIGN_CDECL
namespace XyRust {
template<class Point> bool AlignPath(Point* points,int count,int32_t (&geometry)[4]){
    static_assert(sizeof(Point)==8 && alignof(Point)>=alignof(int32_t) && offsetof(Point,x)==0 && offsetof(Point,y)==4,"Rust POINT layout");
    return xy_vsfilter_path_align(points,count,geometry)!=0;
}
}
