// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_SEEK_CDECL __cdecl
#else
#define XY_SEEK_CDECL
#endif
extern "C" uint32_t XY_SEEK_CDECL xy_vsfilter_seek_target(uint64_t,uint64_t,int64_t,uint32_t,int32_t,int64_t*);
#undef XY_SEEK_CDECL
namespace XyRust {
template<class Offset> bool SeekTarget(uint64_t position,uint64_t length,Offset& offset,uint32_t origin,int bom){
    int64_t result;
    if(!xy_vsfilter_seek_target(position,length,offset,origin,bom,&result))return false;
    offset=result;return true;
}
}
