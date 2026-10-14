// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_PLANE_CDECL __cdecl
#else
#define XY_PLANE_CDECL
#endif
extern "C" {
void* XY_PLANE_CDECL xy_vsfilter_plane_create(size_t);
uint8_t* XY_PLANE_CDECL xy_vsfilter_plane_loan(void*);
void XY_PLANE_CDECL xy_vsfilter_plane_release(void*);
}
#undef XY_PLANE_CDECL
extern size_t g_xy_malloc_used_size;
namespace XyRust {
struct PlaneDeleter {
    void* owner;size_t count;
    PlaneDeleter(void* p,size_t n):owner(p),count(n){}
    void operator()(uint8_t*)const{g_xy_malloc_used_size-=count;xy_vsfilter_plane_release(owner);}
};
class PlaneAllocation {
    void* owner;uint8_t* data;size_t count;
    PlaneAllocation(const PlaneAllocation&);PlaneAllocation& operator=(const PlaneAllocation&);
public:
    explicit PlaneAllocation(size_t n):owner(xy_vsfilter_plane_create(n)),data(NULL),count(n){
        if(owner){data=xy_vsfilter_plane_loan(owner);g_xy_malloc_used_size+=count;}
    }
    ~PlaneAllocation(){Reset();}
    void Reset(){if(owner)PlaneDeleter(owner,count)(data);owner=NULL;data=NULL;}
    bool Ready()const{return owner!=NULL;}
    uint8_t* Data()const{return data;}
    template<class Shared> void Attach(Shared& destination){
        // shared_ptr's constructor invokes the deleter if its control-block
        // allocation throws. Transfer first so this RAII owner cannot repeat it.
        void* transferred=owner;owner=NULL;
        destination.reset(data,PlaneDeleter(transferred,count));
    }
};
}
