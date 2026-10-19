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
uint32_t XY_EDGES_CDECL xy_vsfilter_edges_line_v1(void*,uint32_t,int32_t,int32_t,int32_t,int32_t,uint32_t,uint8_t*);
int64_t XY_EDGES_CDECL xy_vsfilter_edges_query(const void*,uint32_t,size_t);
void XY_EDGES_CDECL xy_vsfilter_edges_release(void*);
void* XY_EDGES_CDECL xy_vsfilter_edges_spans(const void*,int32_t);
uint64_t XY_EDGES_CDECL xy_vsfilter_row_field(const void*,uint32_t,size_t);
void XY_EDGES_CDECL xy_vsfilter_row_release(void*);
}
#undef XY_EDGES_CDECL
namespace XyRust {
class ScanRow {
    void* owner;
    ScanRow(const ScanRow&);ScanRow& operator=(const ScanRow&);
public:
    ScanRow(const void* edges,int row):owner(xy_vsfilter_edges_spans(edges,row)){}
    ~ScanRow(){xy_vsfilter_row_release(owner);}
    bool Ready()const{return owner!=NULL;}
    size_t Count()const{return static_cast<size_t>(xy_vsfilter_row_field(owner,0,0));}
    uint64_t Start(size_t i)const{return xy_vsfilter_row_field(owner,1,i);}
    uint64_t End(size_t i)const{return xy_vsfilter_row_field(owner,2,i);}
};
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
    void Line(unsigned& next,unsigned& capacity,int x0,int y0,int x1,int y1,bool up){
        uint8_t bytes[8];const auto status=xy_vsfilter_edges_line_v1(owner,next,x0,y0,x1,y1,up?1:0,bytes);
        if(status==5)throw std::bad_alloc();if(status)throw std::out_of_range("unresolved scan line geometry");
        uint32_t result[2]={};for(unsigned i=0;i<8;++i)result[i/4]|=uint32_t(bytes[i])<<(8*(i%4));next=result[0];capacity=result[1];
    }
    unsigned Capacity()const{return static_cast<unsigned>(Get(3,0));}
    unsigned Rows()const{return static_cast<unsigned>(Get(4,0));}
    unsigned Head(size_t row)const{return static_cast<unsigned>(Get(0,row));}
    unsigned Next(unsigned index)const{return static_cast<unsigned>(Get(1,index));}
    int Position(unsigned index)const{return static_cast<int>(Get(2,index));}
    template<class Spans> bool AppendRow(int row,Spans& destination)const{
        ScanRow result(owner,row);
        if(!result.Ready())return false;
        for(size_t i=0,n=result.Count();i<n;++i){
            const uint64_t start=result.Start(i),end=result.End(i);
            destination.push_back(typename Spans::value_type(start,end));
        }
        return true;
    }
};
}
