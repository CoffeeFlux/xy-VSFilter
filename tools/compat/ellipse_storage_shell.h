// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#endif
#include "xy_rust_ellipse.h"
static uint64_t ellipseScratchOwners=0,ellipseScratchReads=0,ellipseScratchWrites=0,ellipseScratchFallbacks=0;
namespace XyRust {
class ObservedEllipseScratch : public EllipseScratch {
public:
    explicit ObservedEllipseScratch(int ry):EllipseScratch(ry){if(Ready())++ellipseScratchOwners;else ++ellipseScratchFallbacks;}
    int Get(int y)const{++ellipseScratchReads;return EllipseScratch::Get(y);}
    void Set(int y,int value){++ellipseScratchWrites;EllipseScratch::Set(y,value);}
};
}
#define ASSERT(x) ((void)0)
#define DEBUG_NEW new
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#define min (std::min)
#define max (std::max)
typedef uint64_t XY_POINT;
#define XY_POINT_X(point) ((point)&0xffffffff)
#define XY_POINT_Y(point) ((point)>>32)
static const int MIN_CROSS_LINE=INT32_MIN,MAX_CROSS_LINE=INT32_MAX;
static const int LOOKUP_TABLE_SIZE_RX=64,LOOKUP_TABLE_SIZE_RY=64;
