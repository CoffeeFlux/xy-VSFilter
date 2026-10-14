// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_LINE_CDECL __cdecl
#else
#define XY_LINE_CDECL
#endif
extern "C" {
uint32_t XY_LINE_CDECL xy_vsfilter_line_grow(int32_t*,const int32_t*);
uint32_t XY_LINE_CDECL xy_vsfilter_line_place(const int32_t*,const int32_t*,const int32_t*,int32_t,int32_t*);
}
#undef XY_LINE_CDECL
namespace XyRust {
template<class Size> bool GrowLineSpace(Size& space,int width,int border,int ascent,int descent){
    int32_t current[]={space.cx,space.cy};
    const int32_t line[]={width,border,ascent,descent};
    if(!xy_vsfilter_line_grow(current,line))return false;
    space.cx=current[0];space.cy=current[1];return true;
}
template<class Size,class Rect> bool PlaceLines(Rect& rect,const Size& size,const Rect& margin,const Size& space,int alignment){
    const int32_t bounds[]={size.cx,size.cy},inset[]={margin.left,margin.top,margin.right,margin.bottom},extent[]={space.cx,space.cy};
    int32_t result[4];
    if(!xy_vsfilter_line_place(bounds,inset,extent,alignment,result))return false;
    rect.left=result[0];rect.top=result[1];rect.right=result[2];rect.bottom=result[3];return true;
}
}
