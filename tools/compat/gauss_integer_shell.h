// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "xy_rust_gauss_integer.h"
static uint64_t integerGaussHandled=0,integerGaussFallback=0;
static uint32_t observedIntegerGauss(uint8_t* buffer,uint32_t* scratch,int32_t width,int32_t height,int32_t stride,const uint32_t* xTable,int32_t rx,int32_t gx,const uint32_t* yTable,int32_t ry,int32_t gy) {
    const uint32_t result=xy_vsfilter_gauss_integer(buffer,scratch,width,height,stride,xTable,rx,gx,yTable,ry,gy);
    integerGaussHandled+=result!=0;integerGaussFallback+=result==0;return result;
}
#define xy_vsfilter_gauss_integer observedIntegerGauss
