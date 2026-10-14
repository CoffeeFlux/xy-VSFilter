// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "span_coverage_shell.h"
#include "xy_rust_bilinear.h"
#include <cassert>
#ifndef _WIN32
typedef uint16_t WORD;
#endif
#define ASSERT(x) assert(x)
#define DEBUG_NEW new
typedef BYTE byte;
static uint64_t varianceRows=0;
static void ObservedVarianceRow(uint8_t* p,uint16_t* c,int32_t w,uint32_t x,uint32_t y){
    ++varianceRows;xy_vsfilter_bilinear_row(p,c,w,x,y);
}
#define VARIANCE_CLASS struct Overlay: ::Overlay {Overlay* GetSubpixelVariance(unsigned int,unsigned int);~Overlay(){CleanUp();}};
