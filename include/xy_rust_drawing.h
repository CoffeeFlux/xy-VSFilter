// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _MSC_VER
#define XY_DRAWING_CDECL __cdecl
#else
#define XY_DRAWING_CDECL
#endif
extern "C" {
void* XY_DRAWING_CDECL xy_vsfilter_drawing_create();
void XY_DRAWING_CDECL xy_vsfilter_drawing_copy(void*,const void*);
uint32_t XY_DRAWING_CDECL xy_vsfilter_drawing_edit(void*,uint32_t,size_t,int32_t,int32_t);
uint32_t XY_DRAWING_CDECL xy_vsfilter_drawing_step(void*, uint32_t, int32_t, int32_t);
uint64_t XY_DRAWING_CDECL xy_vsfilter_drawing_field(const void*, uint32_t, size_t);
void XY_DRAWING_CDECL xy_vsfilter_drawing_release(void*);
}
#undef XY_DRAWING_CDECL

namespace XyRust {
class DrawingPath {
    void* owner;
    DrawingPath(const DrawingPath&);
    DrawingPath& operator=(const DrawingPath&);
    static int32_t Signed(uint32_t value) {
        return value <= INT32_MAX ? static_cast<int32_t>(value) :
            -static_cast<int32_t>(UINT32_MAX - value) - 1;
    }
public:
    DrawingPath() : owner(xy_vsfilter_drawing_create()) { if (!owner) abort(); }
    ~DrawingPath() { xy_vsfilter_drawing_release(owner); }
    void Copy(const DrawingPath& source){xy_vsfilter_drawing_copy(owner,source.owner);}
    void Swap(DrawingPath& other){void* previous=owner;owner=other.owner;other.owner=previous;}
    void Clear(){xy_vsfilter_drawing_edit(owner,0,0,0,0);}
    void SetPoint(size_t index,int32_t x,int32_t y){if(!xy_vsfilter_drawing_edit(owner,1,index,x,y))abort();}
    bool Command(uint16_t command) { return xy_vsfilter_drawing_step(owner, 0, command, 0) != 0; }
    bool Point(int32_t x, int32_t y) { return xy_vsfilter_drawing_step(owner, 1, x, y) != 0; }
    bool NoPoint() { return xy_vsfilter_drawing_step(owner, 2, 0, 0) != 0; }
    bool Finish() { return xy_vsfilter_drawing_step(owner, 3, 0, 0) != 0; }
    size_t Count() const { return static_cast<size_t>(xy_vsfilter_drawing_field(owner, 0, 0)); }
    unsigned char Type(size_t index) const {
        return static_cast<unsigned char>(xy_vsfilter_drawing_field(owner, 1, index));
    }
    template<typename NativePoint> NativePoint GetPoint(size_t index) const {
        const uint64_t packed = xy_vsfilter_drawing_field(owner, 2, index);
        return NativePoint(Signed(static_cast<uint32_t>(packed)), Signed(static_cast<uint32_t>(packed >> 32)));
    }
};
}
