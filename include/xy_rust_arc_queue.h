// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <new>
#ifdef _MSC_VER
#define XY_ARC_CDECL __cdecl
#else
#define XY_ARC_CDECL
#endif
extern "C" {
void* XY_ARC_CDECL xy_vsfilter_arc_create(size_t);
void* XY_ARC_CDECL xy_vsfilter_arc_copy(const void*);
uint64_t XY_ARC_CDECL xy_vsfilter_arc_query(const void*,uint32_t,size_t);
int32_t XY_ARC_CDECL xy_vsfilter_arc_edit(void*,uint32_t,size_t,uint64_t,int32_t);
void XY_ARC_CDECL xy_vsfilter_arc_release(void*);
}
#undef XY_ARC_CDECL
namespace XyRust {
template<class Item> class ArcQueue {
    void* owner;
    ArcQueue& operator=(const ArcQueue&);
public:
    ArcQueue():owner(NULL){}
    ArcQueue(const ArcQueue& other):owner(xy_vsfilter_arc_copy(other.owner)){}
    ~ArcQueue(){xy_vsfilter_arc_release(owner);}
    int init(size_t capacity){
        // Renderer queues initialize once, before use. No storage loan escapes.
        if(owner)throw std::bad_alloc();
        owner=xy_vsfilter_arc_create(capacity);if(!owner)throw std::bad_alloc();return 0;
    }
    size_t capacity()const{return static_cast<size_t>(xy_vsfilter_arc_query(owner,0,0));}
    size_t size()const{return static_cast<size_t>(xy_vsfilter_arc_query(owner,1,0));}
    bool empty()const{return !owner || xy_vsfilter_arc_query(owner,2,0)!=0;}
    bool full()const{return xy_vsfilter_arc_query(owner,3,0)!=0;}
    size_t free()const{return static_cast<size_t>(xy_vsfilter_arc_query(owner,4,0));}
    Item get_at(int i)const{Item item;item.arc_center=xy_vsfilter_arc_query(owner,5,i);item.dead_line=static_cast<int32_t>(xy_vsfilter_arc_query(owner,6,i));return item;}
    Item back()const{Item item;item.arc_center=xy_vsfilter_arc_query(owner,7,0);item.dead_line=static_cast<int32_t>(xy_vsfilter_arc_query(owner,8,0));return item;}
    void append(uint64_t center,int deadline){xy_vsfilter_arc_edit(owner,0,0,center,deadline);}
    int push_back(const Item& item){return xy_vsfilter_arc_edit(owner,1,0,item.arc_center,item.dead_line);}
    void pop_front(){xy_vsfilter_arc_edit(owner,2,0,0,0);}
    void pop_back(){xy_vsfilter_arc_edit(owner,3,0,0,0);}
    void pop_last_n(int n){xy_vsfilter_arc_edit(owner,4,n,0,0);}
    void set_center(int i,uint64_t center){xy_vsfilter_arc_edit(owner,5,i,center,0);}
    void set_deadline(int i,int deadline){xy_vsfilter_arc_edit(owner,6,i,0,deadline);}
};
}
