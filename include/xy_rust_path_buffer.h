// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_PATH_BUFFER_CDECL __cdecl
#else
#define XY_PATH_BUFFER_CDECL
#endif
struct alignas(void*) XyPathBuffers {void* points;uint8_t* types;};
static_assert(sizeof(XyPathBuffers)==2*sizeof(void*) && offsetof(XyPathBuffers,types)==sizeof(void*) && alignof(XyPathBuffers)>=alignof(void*),"Rust path buffer pointer layout");
extern "C" {
void* XY_PATH_BUFFER_CDECL xy_vsfilter_path_buffer_create(int32_t);
void XY_PATH_BUFFER_CDECL xy_vsfilter_path_buffer_loan(void*,XyPathBuffers*);
uint32_t XY_PATH_BUFFER_CDECL xy_vsfilter_path_buffer_append(const void*,void*,uint8_t*,int32_t,int32_t);
void XY_PATH_BUFFER_CDECL xy_vsfilter_path_buffer_release(void*);
}
#undef XY_PATH_BUFFER_CDECL
namespace XyRust {
class PathBuffer {
    void* owner;
    PathBuffer(const PathBuffer&);PathBuffer& operator=(const PathBuffer&);
public:
    explicit PathBuffer(int count):owner(xy_vsfilter_path_buffer_create(count)){}
    ~PathBuffer(){Release();}
    bool Ready()const{return owner!=NULL;}
    template<class Point,class Byte> void Loan(Point*& points,Byte*& types){
        static_assert(sizeof(Point)==8 && alignof(Point)>=alignof(int32_t) && offsetof(Point,x)==0 && offsetof(Point,y)==4,"Rust POINT layout");
        static_assert(sizeof(Byte)==1,"Rust path type layout");
        XyPathBuffers buffers;xy_vsfilter_path_buffer_loan(owner,&buffers);
        points=static_cast<Point*>(buffers.points);types=reinterpret_cast<Byte*>(buffers.types);
    }
    template<class Point,class Byte>bool Append(Point* points,Byte* types,int dx,int dy){
        static_assert(sizeof(Point)==8 && alignof(Point)>=alignof(int32_t) && offsetof(Point,x)==0 && offsetof(Point,y)==4,"Rust POINT layout");
        static_assert(sizeof(Byte)==1,"Rust path type layout");
        return xy_vsfilter_path_buffer_append(owner,points,reinterpret_cast<uint8_t*>(types),dx,dy)!=0;
    }
    void Release(){xy_vsfilter_path_buffer_release(owner);owner=NULL;}
};
}
