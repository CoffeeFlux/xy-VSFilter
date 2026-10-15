#include "stdafx.h"
#include "xy_bitmap.h"
#include "xy_malloc.h"
#include "../SubPic/ISubPic.h"

#if ENABLE_XY_LOG_SUB_RENDER_FRAME
#  define TRACE_SUB_RENDER_FRAME(msg) XY_LOG_TRACE(msg)
#else
#  define TRACE_SUB_RENDER_FRAME(msg)
#endif

XyBitmap::~XyBitmap()
{
    xy_free(bits);
}

XyBitmap * XyBitmap::CreateBitmap( const CRect& target_rect )
{
    XyBitmap *result = DEBUG_NEW XyBitmap();
    if (result==NULL)
    {
        ASSERT(0);
        return result;
    }
    result->x = target_rect.left;
    result->y = target_rect.top;
    result->w = target_rect.Width();
    result->h = target_rect.Height();
    int w16 = (result->w + 15) & ~15;
        
    result->pitch = w16*4;
    result->bits = xy_malloc(4*w16*result->h, (result->x*4)&15);
    result->plans[0] = reinterpret_cast<BYTE*>(result->bits);
    ClearBitmap(result);
    return result;
}

void XyBitmap::ClearBitmap( XyBitmap *bitmap )
{
    if (!bitmap)
        return;
    BYTE * p = bitmap->plans[0];
    for (int i=0;i<bitmap->h;i++, p+=bitmap->pitch)
    {
        memsetd(p, 0xFF000000, bitmap->w*4);
    }        
}

void XyBitmap::AlphaBltPack( SubPicDesc& spd, POINT pos, SIZE size, LPCVOID pixels, int pitch )
{
    CRect r(0, 0, spd.w, spd.h);

    int x = pos.x;
    int y = pos.y;
    int w = size.cx;
    int h = size.cy;
    int x_src = 0, y_src = 0;

    if(x < r.left) {x_src = r.left-x; w -= r.left-x; x = r.left;}
    if(y < r.top) {y_src = r.top-y; h -= r.top-y; y = r.top;}
    if(x+w > r.right) w = r.right-x;
    if(y+h > r.bottom) h = r.bottom-y;

    const BYTE* src = reinterpret_cast<const BYTE*>(pixels) + y_src*pitch + x_src*4;

    BYTE* dst = reinterpret_cast<BYTE*>(spd.bits) + spd.pitch * y + ((x*spd.bpp)>>3);

    for(int i=0;i<h;i++, src += pitch, dst += spd.pitch)
    {
        const BYTE* s2 = src;
        const BYTE* s2end = s2 + w*4;
        DWORD* d2 = (DWORD*)dst;
        for(; s2 < s2end; s2 += 4, d2++)
        {
            int tmp = s2[3]+1;
            *d2 = (((((*d2&0x00ff00ff)*tmp)>>8) + (*((DWORD*)s2)&0x00ff00ff))&0x00ff00ff)
                | (((((*d2&0x0000ff00)*tmp)>>8) + (*((DWORD*)s2)&0x0000ff00))&0x0000ff00);
        }
    }
}


//////////////////////////////////////////////////////////////////////////
//
// SubRenderFrame
// 

XySubRenderFrame::XySubRenderFrame()
{

}

XySubRenderFrame::~XySubRenderFrame()
{
}

HRESULT XySubRenderFrame::GetBitmapCount( int *count )
{
    TRACE_SUB_RENDER_FRAME(count<<" "<<m_bitmaps.GetCount());
    if (!count)
    {
        return E_POINTER;
    }
    *count = m_bitmaps.GetCount();
    return S_OK;
}

HRESULT XySubRenderFrame::GetBitmap( int index, ULONGLONG *id, POINT *position, SIZE *size, LPCVOID *pixels, int *pitch )
{
    if (index<0 || index>=(int)m_bitmaps.GetCount())
    {
        return E_INVALIDARG;
    }
    if (id)
    {
        *id = m_bitmap_ids.GetAt(index);
    }
    const XyBitmap& bitmap = *(m_bitmaps.GetAt(index));
    if (position)
    {
        position->x = bitmap.x + m_output_rect.left;
        position->y = bitmap.y + m_output_rect.top;
    }
    if (size)
    {
        size->cx = bitmap.w;
        size->cy = bitmap.h;
    }
    if (pixels)
    {
        *pixels = bitmap.plans[0];
    }
    if (pitch)
    {
        *pitch = bitmap.pitch;
    }
    TRACE_SUB_RENDER_FRAME(index<<" id: "<<m_bitmap_ids.GetAt(index)
        <<" dirty_rect: x "<<bitmap.x+m_output_rect.left<<", y "<<bitmap.y + m_output_rect.top
        <<", w "<<bitmap.w<<", h "<<bitmap.h
        <<" pitch:"<<bitmap.pitch<<" pixels:"<<(void*)bitmap.plans[0]);
    return S_OK;
}

void XySubRenderFrame::MoveTo( int x, int y )
{
    m_left = x;
    m_top  = y;
    m_output_rect.MoveToXY(x, y);
    m_clip_rect.MoveToXY(x, y);
}

//////////////////////////////////////////////////////////////////////////
//
// XySubRenderFrameCreater
// 

XySubRenderFrameCreater* XySubRenderFrameCreater::GetDefaultCreater()
{
    static XySubRenderFrameCreater s_default_creater;
    return &s_default_creater;
}

HRESULT XySubRenderFrameCreater::SetOutputRect( const RECT& output_rect )
{
    m_output_rect = output_rect;
    return S_OK;
}

HRESULT XySubRenderFrameCreater::SetClipRect( const RECT& clip_rect )
{
    m_clip_rect = clip_rect;
    return S_OK;
}

XySubRenderFrame* XySubRenderFrameCreater::NewXySubRenderFrame( UINT bitmap_count )
{
    XySubRenderFrame *result = DEBUG_NEW XySubRenderFrame();
    ASSERT(result);
    result->m_output_rect = m_output_rect;
    result->m_clip_rect = m_clip_rect;
    result->m_bitmap_ids.SetCount(bitmap_count);
    result->m_bitmaps.SetCount(bitmap_count);
    return result;
}

XyBitmap* XySubRenderFrameCreater::CreateBitmap( const RECT& target_rect )
{
    return XyBitmap::CreateBitmap(target_rect);
}


