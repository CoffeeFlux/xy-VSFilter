// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdexcept>
#ifdef _MSC_VER
#define XY_ELLIPSE_CDECL __cdecl
#else
#define XY_ELLIPSE_CDECL
#endif
extern "C" {
void* XY_ELLIPSE_CDECL xy_vsfilter_ellipse_create(int32_t,int32_t);
void* XY_ELLIPSE_CDECL xy_vsfilter_ellipse_owned_create(int32_t,int32_t);
int64_t XY_ELLIPSE_CDECL xy_vsfilter_ellipse_cell(const void*,size_t);
void XY_ELLIPSE_CDECL xy_vsfilter_ellipse_release(void*);
void* XY_ELLIPSE_CDECL xy_vsfilter_ellipse_matrix_create(int32_t,int32_t);
uint32_t XY_ELLIPSE_CDECL xy_vsfilter_ellipse_matrix_set(void*,int32_t,int32_t,int32_t);
int64_t XY_ELLIPSE_CDECL xy_vsfilter_ellipse_matrix_get(const void*,int32_t,int32_t);
void XY_ELLIPSE_CDECL xy_vsfilter_ellipse_matrix_release(void*);
}
#undef XY_ELLIPSE_CDECL
namespace XyRust {
class EllipseMatrix {
    void* owner;
    EllipseMatrix(const EllipseMatrix&);EllipseMatrix& operator=(const EllipseMatrix&);
public:
    EllipseMatrix():owner(NULL){}
    ~EllipseMatrix(){xy_vsfilter_ellipse_matrix_release(owner);}
    bool Initialize(int rx,int ry){
        void* next=xy_vsfilter_ellipse_matrix_create(rx,ry);
        xy_vsfilter_ellipse_matrix_release(owner);owner=next;return owner!=NULL;
    }
    bool Ready()const{return owner!=NULL;}
    void Set(int dy,int dx,int value){
        if(!xy_vsfilter_ellipse_matrix_set(owner,dy,dx,value))throw std::out_of_range("ellipse matrix coordinate");
    }
    int Get(int dy,int dx)const{
        int64_t value=xy_vsfilter_ellipse_matrix_get(owner,dy,dx);
        if(value==INT64_MIN)throw std::out_of_range("ellipse matrix coordinate");
        return static_cast<int>(value);
    }
};
class EllipseArc {
    void* owner;
    uint64_t origin;
    EllipseArc(const EllipseArc&);EllipseArc& operator=(const EllipseArc&);
public:
    EllipseArc():owner(NULL),origin(0){}
    ~EllipseArc(){xy_vsfilter_ellipse_release(owner);}
    bool Initialize(int rx,int ry){
        void* next=xy_vsfilter_ellipse_owned_create(rx,ry);
        xy_vsfilter_ellipse_release(owner);owner=next;origin=static_cast<uint64_t>(ry);
        return owner!=NULL;
    }
    bool Ready()const{return owner!=NULL;}
    int AtOffset(uint64_t offset)const{
        // Keep the native unsigned relative-coordinate calculation. Adding
        // the center wraps it back to a base-relative index for negative offsets.
        uint64_t index=origin+offset;
        if(index>SIZE_MAX)throw std::out_of_range("ellipse coordinate");
        int64_t value=xy_vsfilter_ellipse_cell(owner,static_cast<size_t>(index));
        if(value==INT64_MIN)throw std::out_of_range("ellipse coordinate");
        return static_cast<int>(value);
    }
};
inline bool GenerateEllipseArc(int* cells,int rx,int ry){
    if(!cells)return false;
    void* owner=xy_vsfilter_ellipse_create(rx,ry);if(!owner)return false;
    size_t count=static_cast<size_t>(ry)*2+1;
    for(size_t i=0;i<count;++i){int64_t value=xy_vsfilter_ellipse_cell(owner,i);if(value!=INT64_MIN)cells[i]=static_cast<int>(value);}
    xy_vsfilter_ellipse_release(owner);return true;
}
}
