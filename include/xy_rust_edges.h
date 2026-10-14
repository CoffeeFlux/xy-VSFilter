// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <new>
#include <stdexcept>
#ifdef _MSC_VER
#define XY_EDGES_CDECL __cdecl
#else
#define XY_EDGES_CDECL
#endif
extern "C" {
void* XY_EDGES_CDECL xy_vsfilter_edges_create(int32_t,uint32_t);
uint32_t XY_EDGES_CDECL xy_vsfilter_edges_insert(void*,uint32_t,int32_t,int32_t);
uint32_t XY_EDGES_CDECL xy_vsfilter_edges_resize(void*,uint32_t);
int64_t XY_EDGES_CDECL xy_vsfilter_edges_query(const void*,uint32_t,size_t);
void XY_EDGES_CDECL xy_vsfilter_edges_release(void*);
}
#undef XY_EDGES_CDECL
namespace XyRust {
class EdgeStorage {
    void* owner;
    EdgeStorage(const EdgeStorage&);EdgeStorage& operator=(const EdgeStorage&);
    int64_t Get(uint32_t field,size_t index)const{int64_t value=xy_vsfilter_edges_query(owner,field,index);if(value==INT64_MIN)throw std::out_of_range("scan edge index");return value;}
public:
    EdgeStorage():owner(NULL){}
    ~EdgeStorage(){Reset();}
    bool Initialize(int height,unsigned capacity){Reset();owner=xy_vsfilter_edges_create(height,capacity);return owner!=NULL;}
    bool Ready()const{return owner!=NULL;}
    void Reset(){xy_vsfilter_edges_release(owner);owner=NULL;}
    void Resize(unsigned capacity){if(!xy_vsfilter_edges_resize(owner,capacity))throw std::bad_alloc();}
    void Insert(unsigned index,int row,int position){if(!xy_vsfilter_edges_insert(owner,index,row,position))throw std::out_of_range("scan edge index");}
    unsigned Capacity()const{return static_cast<unsigned>(Get(3,0));}
    unsigned Rows()const{return static_cast<unsigned>(Get(4,0));}
    unsigned Head(size_t row)const{return static_cast<unsigned>(Get(0,row));}
    unsigned Next(unsigned index)const{return static_cast<unsigned>(Get(1,index));}
    int Position(unsigned index)const{return static_cast<int>(Get(2,index));}
};
}
