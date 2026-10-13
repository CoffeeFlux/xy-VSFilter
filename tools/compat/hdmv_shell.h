// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include "xy_rust_hdmv.h"
typedef uint8_t BYTE;
typedef uint8_t byte;
typedef uint32_t DWORD;
typedef uint64_t UINT64;
typedef int64_t INT64;
using std::min;
#define ASSERT(x) ((void)0)
#ifndef _MSC_VER
#define __forceinline inline
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#define HDMV_NATIVE_PIXELS 1
#include <emmintrin.h>
#else
#define HDMV_NATIVE_PIXELS 0
#endif
enum { MSP_AYUV_PLANAR=1 };
namespace DM { enum { AYUV_PLANAR=1 }; }
struct SubPicDesc { int type=0, pitch=0, h=0; BYTE* bits=nullptr; };
class Rasterizer {
public:
    static void FillSolidRect(SubPicDesc&,int,int,int,int,DWORD);
};
typedef std::array<int64_t,7> HdmvEvent;
static std::function<void(HdmvEvent)> hdmvObserve;
static bool hdmvPixels=false, hdmvChangePalette=false;
struct HdmvObject {
    BYTE* m_pRLEData=nullptr;
    int m_nRLEDataSize=0;
    short m_horizontal_position=0,m_vertical_position=0,m_height=0;
    DWORD m_Colors[256];
    bool palette=true;
    bool HavePalette() { hdmvObserve({{0,palette,0,0,0,0,0}}); return palette; }
    void FillSolidRect(SubPicDesc& spd,int x,int y,int width,int height,DWORD color) {
        hdmvObserve({{3,x,y,width,height,color,0}});
        if (hdmvPixels) {
            // Pixel cases are constructed to keep every original draw in the
            // actual surface. Other malformed cases compare draw requests only.
            const int row_width=spd.type==MSP_AYUV_PLANAR ? spd.pitch : spd.pitch/4;
            if(x<0 || y<0 || width<1 || x+width>row_width || y+height>spd.h) throw 19;
#if HDMV_NATIVE_PIXELS
            Rasterizer::FillSolidRect(spd,x,y,width,height,color);
#endif
        }
        if(hdmvChangePalette) for(auto& c:m_Colors)c^=0x35ab91f7u;
    }
};
class OriginalHdmv : public HdmvObject { public: void RenderHdmv(SubPicDesc&); };
class CandidateHdmv : public HdmvObject { public: void RenderHdmv(SubPicDesc&); };
