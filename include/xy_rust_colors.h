// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <limits.h>
// Internal synchronous entry; no public DLL export or retained native pointer.
extern "C" uint32_t __cdecl xy_vsfilter_html_color_v1(
    uint32_t,const uint16_t*,uint32_t,const uint16_t*,uint32_t,uint16_t*,uint32_t);
namespace XyRust {
inline DWORD HtmlColor(const CStringW& input) {
    uint16_t out[2];
    if(xy_vsfilter_html_color_v1(0,reinterpret_cast<const uint16_t*>(input.GetString()),input.GetLength(),nullptr,0,out,2))AtlThrow(E_UNEXPECTED);
    return static_cast<DWORD>(out[0])|(static_cast<DWORD>(out[1])<<16);
}
inline CStringW HtmlColorTag(const CStringW& input,const CStringW& tag) {
    if(tag.GetLength()>INT_MAX-12)AtlThrow(E_OUTOFMEMORY);
    const int length=tag.GetLength()+12;
    CStringW result;WCHAR* out=result.GetBufferSetLength(length);
    if(xy_vsfilter_html_color_v1(1,reinterpret_cast<const uint16_t*>(input.GetString()),input.GetLength(),reinterpret_cast<const uint16_t*>(tag.GetString()),tag.GetLength(),reinterpret_cast<uint16_t*>(out),length))AtlThrow(E_UNEXPECTED);
    result.ReleaseBufferSetLength(length);return result;
}
}
