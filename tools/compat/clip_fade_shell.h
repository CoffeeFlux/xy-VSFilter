// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "clip_mask_shell.h"
using std::min;
#define ASSERT(x) do{if(!(x))std::abort();}while(0)
struct FadePolygon {double m_target_scale_x,m_target_scale_y;};
struct FadeEffect {int param[8];};
static bool fadeNull=false;
static std::vector<BYTE> fadeInput;
static uint64_t bannerCalls=0,bannerDeclines=0,scrollCalls=0,scrollDeclines=0;
static uint32_t observedBannerFade(BYTE* data,int w,int h,int fade,int delta){
    clipCheckRange(data,static_cast<size_t>(w)*h);uint32_t result=xy_vsfilter_banner_fade(data,w,h,fade,delta);
    if(result)++bannerCalls;else ++bannerDeclines;return result;
}
static uint32_t observedScrollFade(BYTE* data,int w,int h,int first,int last,int alpha,int delta,uint32_t flags){
    clipCheckRange(data,static_cast<size_t>(w)*h);uint32_t result=xy_vsfilter_scroll_fade(data,w,h,first,last,alpha,delta,flags);
    if(result)++scrollCalls;else ++scrollDeclines;return result;
}
#define CLIP_FADE_CLASS class CClipper {public: \
    CSize m_size;FadePolygon polygon;FadePolygon* m_polygon;FadeEffect m_effect; \
    CClipper(int w,int h,int fade,double scale,int top,int bottom):m_size(w,h),m_polygon(&polygon){ \
        polygon.m_target_scale_x=polygon.m_target_scale_y=scale;std::memset(&m_effect,0,sizeof(m_effect)); \
        m_effect.param[0]=top;m_effect.param[1]=bottom;m_effect.param[2]=m_effect.param[4]=fade; \
    } \
    GrayImage2* PaintBaseClipper(){coverageEvents.push_back(9);if(fadeNull)return NULL; \
        auto* output=new GrayImage2();output->pitch=m_size.cx;output->size=m_size;output->left_top=CPoint(0,0); \
        output->data.reset(static_cast<BYTE*>(xy_malloc(fadeInput.size())),xy_free); \
        std::memcpy(output->data.get(),fadeInput.data(),fadeInput.size());return output; \
    } \
    GrayImage2* PaintBannerClipper();GrayImage2* PaintScrollClipper(); \
};
