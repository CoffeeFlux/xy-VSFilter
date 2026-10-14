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
extern "C" uint64_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_clip(int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t x, int32_t y, int32_t length);
extern "C" uint32_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_fill(uint8_t* destination, size_t pixels, uint32_t color);
extern "C" uint32_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_nibble(uint8_t byte, uint32_t aligned);
extern "C" uint64_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_code_step(uint32_t code, uint8_t nibble, uint32_t stage);
extern "C" uint32_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_bounds(const uint8_t* source, int32_t width, int32_t height, int32_t* output);
extern "C" uint32_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_crop(const uint8_t* source, int32_t pitch, int32_t width, int32_t height, uint8_t* destination, const uint8_t* protected_object, size_t protected_length);
extern "C" uint32_t XY_VOBSUB_CDECL xy_vsfilter_vobsub_scale(const uint32_t* source, uint32_t* destination, int32_t width, int32_t height);
#undef XY_VOBSUB_CDECL
