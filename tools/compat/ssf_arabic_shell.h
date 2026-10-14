// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cfenv>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#else
typedef char16_t WCHAR;
#endif
#include "xy_rust_ssf_arabic.h"
static uint64_t arabicClassifications=0,arabicForms=0,arabicNeighbors=0;
static uint32_t observedArabic(uint16_t c){++arabicClassifications;return xy_vsfilter_ssf_is_arabic(c);}
static uint32_t observedForm(uint16_t c,uint32_t f){++arabicForms;return xy_vsfilter_ssf_arabic_form(c,f);}
static uint32_t observedNeighbors(uint16_t c,uint16_t p,uint16_t n){++arabicNeighbors;return xy_vsfilter_ssf_arabic_neighbors(c,p,n);}
#define xy_vsfilter_ssf_is_arabic observedArabic
#define xy_vsfilter_ssf_arabic_form observedForm
#define xy_vsfilter_ssf_arabic_neighbors observedNeighbors
