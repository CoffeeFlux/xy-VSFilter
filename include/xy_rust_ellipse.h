// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_ELLIPSE_CDECL __cdecl
#else
#define XY_ELLIPSE_CDECL
#endif
extern "C" {
void* XY_ELLIPSE_CDECL xy_vsfilter_ellipse_create(int32_t,int32_t);
int64_t XY_ELLIPSE_CDECL xy_vsfilter_ellipse_cell(const void*,size_t);
void XY_ELLIPSE_CDECL xy_vsfilter_ellipse_release(void*);
}
#undef XY_ELLIPSE_CDECL
namespace XyRust {
inline bool GenerateEllipseArc(int* cells,int rx,int ry){
    if(!cells)return false;
    void* owner=xy_vsfilter_ellipse_create(rx,ry);if(!owner)return false;
    size_t count=static_cast<size_t>(ry)*2+1;
    for(size_t i=0;i<count;++i){int64_t value=xy_vsfilter_ellipse_cell(owner,i);if(value!=INT64_MIN)cells[i]=static_cast<int>(value);}
    xy_vsfilter_ellipse_release(owner);return true;
}
}
