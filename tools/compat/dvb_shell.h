// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "xy_rust_dvb.h"
struct RECT { int32_t left,top,right,bottom; };
struct CRect : RECT {
    CRect(int32_t l,int32_t t,int32_t r,int32_t b) { left=l;top=t;right=r;bottom=b; }
};
#define FALSE 0
#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall UnionRect(RECT*,const RECT*,const RECT*);
#else
// Storage/API stand-in on portable platforms; Windows calls the real API.
static int UnionRect(RECT* dst,const RECT* a,const RECT* b) {
    const bool empty_a=a->left>=a->right || a->top>=a->bottom;
    const bool empty_b=b->left>=b->right || b->top>=b->bottom;
    if(empty_a) { *dst=empty_b ? RECT{0,0,0,0} : *b;return !empty_b; }
    if(empty_b) { *dst=*a;return 1; }
    const RECT result={std::min(a->left,b->left),std::min(a->top,b->top),std::max(a->right,b->right),std::max(a->bottom,b->bottom)};
    *dst=result;return 1;
}
#endif
static int ObservedUnionRect(RECT* dst,const RECT* a,const RECT* b) {
    hdmvObserve({{5,b->left,b->top,b->right,b->bottom,0,0}});
    const int result=UnionRect(dst,a,b);
    hdmvObserve({{6,dst->left,dst->top,dst->right,dst->bottom,result,0}});
    return result;
}
class ObservedDvbBuffer : public ObservedGolomb {
public:
    ObservedDvbBuffer(BYTE* data,int size):ObservedGolomb(data,size){}
    short ReadShort() {
        const short value=ReferenceGolomb::ReadShort();
        hdmvObserve({{8,m_nBitPos,m_bitlen,m_bitbuff,value,0,0}});
        return value;
    }
    int GetPos() {
        const int value=ReferenceGolomb::GetPos();
        hdmvObserve({{9,m_nBitPos,m_bitlen,m_bitbuff,value,0,0}});
        return value;
    }
    void SkipBytes(int count) {
        ReferenceGolomb::SkipBytes(count);
        hdmvObserve({{10,count,m_nBitPos,m_bitlen,m_bitbuff,0,0}});
    }
    UINT64 BitRead(int bits,bool peek=false) {
        const UINT64 value=ReferenceGolomb::BitRead(bits,peek);
        hdmvObserve({{4,bits,m_nBitPos,m_bitlen,m_bitbuff,static_cast<int64_t>(value),peek}});
        return value;
    }
    void BitByteAlign() {
        const int before=m_bitlen;
        ReferenceGolomb::BitByteAlign();
        hdmvObserve({{7,before,m_bitlen,m_bitbuff,m_nBitPos,0,0}});
    }
};
#define DVB_SHELL(Name) \
class Name : public HdmvObject { \
public: \
    short m_width=0; \
    void RenderDvb(SubPicDesc&,short,short,RECT*); \
    void DvbRenderField(SubPicDesc&,ObservedDvbBuffer&,short,short,short,RECT*); \
    void Dvb2PixelsCodeString(SubPicDesc&,ObservedDvbBuffer&,short&,short&,RECT*); \
    void Dvb4PixelsCodeString(SubPicDesc&,ObservedDvbBuffer&,short&,short&,RECT*); \
    void Dvb8PixelsCodeString(SubPicDesc&,ObservedDvbBuffer&,short&,short&,RECT*); \
    void Run(int mode,SubPicDesc& s,ObservedDvbBuffer& b,short& x,short& y,RECT* r) { \
        if(mode==2)Dvb2PixelsCodeString(s,b,x,y,r); \
        else if(mode==4)Dvb4PixelsCodeString(s,b,x,y,r); \
        else Dvb8PixelsCodeString(s,b,x,y,r); \
    } \
};
