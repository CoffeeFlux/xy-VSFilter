// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_VOBSUB_CDECL __cdecl
#else
#define XY_VOBSUB_CDECL
#endif
struct XyVobSubPacketPatch {
    uint32_t flags;
    int32_t current;
    uint32_t forced;
    int32_t delay;
    int32_t left,top,right,bottom;
    size_t offsets[2];
    uint32_t palette,transparency,animated;
};
extern "C" uint32_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_packet(const uint8_t* packet, size_t length, size_t data_size, int32_t time, XyVobSubPacketPatch* output);
#undef XY_VOBSUB_CDECL
