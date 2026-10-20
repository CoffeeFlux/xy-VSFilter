/*
 *	Copyright (C) 2003-2006 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include <memory>
#include <vector>
#include "../SubPic/ISubPic.h"
#include "xy_malloc.h"
#ifdef XY_VSFILTER_RUST_TEXT
#include "xy_rust_edges.h"
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

#define PT_MOVETONC 0xfe
#define PT_BSPLINETO 0xfc
#define PT_BSPLINEPATCHTO 0xfa

typedef struct {
    int left, top;
    int w, h;                   // width, height
    unsigned char *buffer;      // w x h buffer
} Bitmap;

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include "xy_rust_path_data.h"
#include "xy_rust_scan_data.h"
#include "xy_rust_overlay.h"
#include "xy_rust_clipper.h"
#endif
struct PathData
{
public:
    PathData();
    ~PathData();

    PathData(const PathData&);//important! disable default copy constructor
    const PathData& operator=(const PathData&);
    bool operator==(const PathData& rhs)const;

    void _TrashPath();
    bool BeginPath(HDC hdc);
    bool EndPath(HDC hdc);
    bool PartialBeginPath(HDC hdc, bool bClearPath);
    bool PartialEndPath(HDC hdc, long dx, long dy);
    
    void AlignLeftTop(CPoint *left_top, CSize *size);

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    bool Scan(XyRust::ScanOwner& scan,const CSize& size)const{return scan.Convert(m_path,size.cx,size.cy);}
    XyRust::PathSnapshot Snapshot()const{return m_path.Read();}
    void Replace(const XyRust::PathSnapshot& value){m_path.Replace(value);}
    void Transform(const uint64_t* values,long x,long y,bool round){m_path.Transform(values,x,y,round);}
    int Count()const{return m_path.Count();}
    ULONG Hash()const{return m_path.Hash();}
    bool Text(HDC dc,uintptr_t font,uint64_t spacing,const uint16_t* units,size_t count){return m_path.Text(dc,font,spacing,units,count);}
private:
    XyRust::PathOwner m_path;
#else
    BYTE* mpPathTypes;
    POINT* mpPathPoints;
    int mPathPoints;
#endif
};

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
typedef PathData PathDataRead;
#endif

typedef std::shared_ptr<const PathData> SharedPtrConstPathData;
typedef std::shared_ptr<PathData> SharedPtrPathData;


typedef std::pair<unsigned __int64, unsigned __int64> tSpan;
typedef std::vector<tSpan> tSpanBuffer;

class ScanLineData
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::ScanOwner m_scan;
#else
    bool fFirstSet;
    CPoint firstp, lastp;

private:
    int mWidth, mHeight;

    tSpanBuffer mOutline;

    enum {
        LINE_DOWN,
        LINE_UP
    };

    struct Edge {
        int next;
        int posandflag;
    } *mpEdgeBuffer;
    unsigned mEdgeHeapSize;
    unsigned mEdgeNext;

    unsigned int* mpScanBuffer;
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::EdgeStorage m_edges;
#endif

    typedef unsigned char byte;

private:
    void _ReallocEdgeBuffer(unsigned edges);
    void _AddEdge(int row, int position);
    void _EvaluateBezier(const PathDataRead& path_data, int ptbase, bool fBSpline);
    void _EvaluateLine(const PathDataRead& path_data, int pt1idx, int pt2idx);
    void _EvaluateLine(int x0, int y0, int x1, int y1);	
    // The following function is templated and forcingly inlined for performance sake
    template<int flag> __forceinline void _EvaluateLine(int x0, int y0, int x1, int y1);

#endif
public:
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::ScanSnapshot Snapshot()const{return m_scan.Read();}
#endif
    ScanLineData();
    virtual ~ScanLineData();

    bool ScanConvert(const PathData& path_data, const CSize& size);

    friend class Rasterizer;
    friend class ScanLineData2;
};

typedef std::shared_ptr<const ScanLineData> SharedPtrConstScanLineData;
typedef std::shared_ptr<ScanLineData> SharedPtrScanLineData;

class ScanLineData2
{
public:
    ScanLineData2():mPathOffsetX(0),mPathOffsetY(0)
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        ,mWideBorder(0)
#endif
    {}
    ScanLineData2(const CPoint& left_top, const SharedPtrConstScanLineData& scan_line_data)
        :m_scan_line_data(scan_line_data)
        ,mPathOffsetX(left_top.x)
        ,mPathOffsetY(left_top.y)
    {
    }
    bool CreateWidenedRegion(int borderX, int borderY);
private:
    SharedPtrConstScanLineData m_scan_line_data;
    int mPathOffsetX, mPathOffsetY;	
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::RegionOwner m_region;
public:
    XyRust::RegionSnapshot Snapshot()const{return m_region.Read();}
private:
#else
    tSpanBuffer mWideOutline;
    int mWideBorder;
#endif

    friend class Rasterizer;
};

typedef std::shared_ptr<const ScanLineData2> SharedPtrConstScanLineData2;
typedef std::shared_ptr<ScanLineData2> SharedPtrScanLineData2;

typedef std::shared_ptr<BYTE> SharedPtrByte;

struct Overlay
{
public:
    Overlay()
    {
        mOffsetX=mOffsetY=mWidth=mHeight=0;
        mOverlayWidth=mOverlayHeight=mOverlayPitch=0;
        mfWideOutlineEmpty = false;
    }
    ~Overlay()
    {
        CleanUp();       
    }

    void CleanUp()
    {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        m_owner.Clear();
#endif
        mBody.reset((BYTE*)NULL);
        mBorder.reset((BYTE*)NULL);
        mOffsetX=mOffsetY=mWidth=mHeight=0;
        mOverlayWidth=mOverlayHeight=mOverlayPitch=0;
        mfWideOutlineEmpty = false;
    }

    void FillAlphaMash(byte* outputAlphaMask, bool fBody, bool fBorder, 
        int x, int y, int w, int h, 
        const byte* pAlphaMask, int pitch, DWORD color_alpha);

    Overlay* GetSubpixelVariance(unsigned int xshift, unsigned int yshift);
    void SetOffsets(int x,int y){
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        m_owner.Offset(x,y);
#endif
        mOffsetX=x;mOffsetY=y;
    }
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
private:
    XyRust::OverlayOwner m_owner;
    void ImportNative(bool gaussian=false,int padding_rows=0);
    bool RestoreRust();
    friend class Rasterizer;
    friend class CClipper;
#endif
public:
    SharedPtrByte mBody;
    SharedPtrByte mBorder;
    int mOffsetX, mOffsetY;
    int mWidth, mHeight;
        
    int mOverlayWidth, mOverlayHeight, mOverlayPitch;

    bool mfWideOutlineEmpty;//specially for blur
};

typedef std::shared_ptr<Overlay> SharedPtrOverlay;

struct GrayImage2
{
public:
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::ClipMaskOwner m_owner;
    bool RestoreRust(const Overlay&);
#endif
    CPoint left_top;
    CSize size;
    int pitch;
    SharedPtrByte data;
};

typedef std::shared_ptr<GrayImage2> SharedPtrGrayImage2;

class XyBitmap;
class Rasterizer
{
private:
    typedef unsigned char byte;

    struct DM
    {
        enum
        {
            SSE2 = 1,
            ALPHA_MASK = 1<<1,
            SINGLE_COLOR = 1<<2,
            BODY = 1<<3,
            AYUV_PLANAR = 1<<4
        };
    };
public:
    static const float GAUSSIAN_BLUR_THREHOLD;
public:

    static bool Rasterize(const ScanLineData2& scan_line_data2, int xsub, int ysub, SharedPtrOverlay overlay);

    static bool IsItReallyBlur(float be_strength, double gaussian_blur_strength);

    static bool Blur(const Overlay& input_overlay, float be_strength, double gaussian_blur_strength, 
        double target_scale_x, double target_scale_y, SharedPtrOverlay output_overlay);

    static bool BeBlur(const Overlay& input_overlay, float be_strength, float target_scale_x, float target_scale_y, 
        SharedPtrOverlay output_overlay);

    static bool GaussianBlur( const Overlay& input_overlay, double gaussian_blur_strength, 
        double target_scale_x, double target_scale_y, 
        SharedPtrOverlay output_overlay );

    static SharedPtrByte CompositeAlphaMask(const SharedPtrOverlay& overlay, const CRect& clipRect, 
        const GrayImage2* alpha_mask, 
        int xsub, int ysub, const DWORD* switchpts, bool fBody, bool fBorder, 
        CRect *outputDirtyRect);

    static void Draw(XyBitmap* bitmap, 
        SharedPtrOverlay overlay, 
        const CRect& clipRect, 
        byte* s_base, 
        int xsub, int ysub, 
        const DWORD* switchpts, bool fBody, bool fBorder);

    static void FillSolidRect(SubPicDesc& spd, int x, int y, int nWidth, int nHeight, DWORD lColor);

    static void AdditionDraw(XyBitmap         *bitmap   , 
                             SharedPtrOverlay  overlay  , 
                             const CRect      &clipRect , 
                             byte             *s_base   , 
                             int               xsub     ,
                             int               ysub     ,
                             const DWORD      *switchpts, 
                             bool              fBody    , 
                             bool              fBorder);
};

