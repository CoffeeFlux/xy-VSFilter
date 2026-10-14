// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#endif
#include "xy_rust_arc_queue.h"
#include "original_arc_queue.h"
typedef uint64_t XY_POINT;
#define XY_POINT_Y(point) ((point)>>32)
#define ASSERT(x) ((void)0)
static const int MIN_CROSS_LINE=INT32_MIN,MAX_CROSS_LINE=INT32_MAX;
struct LinkArcItem {uint64_t arc_center;int dead_line;LinkArcItem():arc_center(0),dead_line(0){}};
struct ArcEllipse {
    int m_rx,m_ry; size_t cursor; std::vector<int> replies;std::vector<uint64_t> calls;
    ArcEllipse(int ry):m_rx(17),m_ry(ry),cursor(0){}
    int cross_left(uint64_t a,uint64_t b){return call(0,a,b);}
    int cross_right(uint64_t a,uint64_t b){return call(1,a,b);}
    int call(int side,uint64_t a,uint64_t b){calls.push_back(side);calls.push_back(a);calls.push_back(b);return replies[cursor++%replies.size()];}
};
