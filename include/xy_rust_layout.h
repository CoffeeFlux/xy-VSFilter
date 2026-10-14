// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef _MSC_VER
#define XY_LAYOUT_CDECL __cdecl
#else
#define XY_LAYOUT_CDECL
#endif
extern "C" {
void* XY_LAYOUT_CDECL xy_vsfilter_layout_create();
void XY_LAYOUT_CDECL xy_vsfilter_layout_add(void*,const int32_t*);
void XY_LAYOUT_CDECL xy_vsfilter_layout_edit(void*,uint32_t,size_t,int32_t);
size_t XY_LAYOUT_CDECL xy_vsfilter_layout_count(const void*);
int32_t XY_LAYOUT_CDECL xy_vsfilter_layout_field(const void*,size_t,uint32_t);
size_t XY_LAYOUT_CDECL xy_vsfilter_layout_find(const void*,int32_t,int32_t);
void XY_LAYOUT_CDECL xy_vsfilter_layout_release(void*);
}
#undef XY_LAYOUT_CDECL
namespace XyRust {
class LayoutRects {
    void* owner;
    LayoutRects(const LayoutRects&);
    LayoutRects& operator=(const LayoutRects&);
public:
    LayoutRects():owner(xy_vsfilter_layout_create()){}
    ~LayoutRects(){xy_vsfilter_layout_release(owner);}
    size_t GetCount()const{return xy_vsfilter_layout_count(owner);}
    void RemoveAll(){xy_vsfilter_layout_edit(owner,0,0,0);}
    void Remove(size_t index){xy_vsfilter_layout_edit(owner,1,index,0);}
    void Segment(size_t index,int segment){xy_vsfilter_layout_edit(owner,2,index,segment);}
    size_t Find(int segment,int entry)const{return xy_vsfilter_layout_find(owner,segment,entry);}
    template<class Record> void AddTail(const Record& record){
        const int32_t fields[]={record.r.left,record.r.top,record.r.right,record.r.bottom,record.segment,record.entry,record.layer};
        xy_vsfilter_layout_add(owner,fields);
    }
    template<class Record> Record Get(size_t index)const{
        Record record;
        record.r.left=xy_vsfilter_layout_field(owner,index,0);
        record.r.top=xy_vsfilter_layout_field(owner,index,1);
        record.r.right=xy_vsfilter_layout_field(owner,index,2);
        record.r.bottom=xy_vsfilter_layout_field(owner,index,3);
        record.segment=xy_vsfilter_layout_field(owner,index,4);
        record.entry=xy_vsfilter_layout_field(owner,index,5);
        record.layer=xy_vsfilter_layout_field(owner,index,6);
        return record;
    }
};
}
