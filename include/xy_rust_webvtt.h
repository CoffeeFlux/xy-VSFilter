// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_WEBVTT_CDECL __cdecl
#else
#define XY_WEBVTT_CDECL
#endif
extern "C" uint32_t XY_WEBVTT_CDECL xy_vsfilter_webvtt_prefix(const uint16_t*, uint32_t);
#undef XY_WEBVTT_CDECL
namespace XyRust {
template<class String> int WebVTTCuePrefix(const String& input){
    const int length=input.GetLength();if(length<=0)return 0;
    std::vector<uint16_t> units(static_cast<size_t>(length));
    for(int i=0;i<length;++i)units[i]=static_cast<uint16_t>(input[i]);
    return static_cast<int>(xy_vsfilter_webvtt_prefix(units.data(),static_cast<uint32_t>(length)));
}
}
