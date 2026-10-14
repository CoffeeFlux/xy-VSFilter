// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#ifdef _MSC_VER
#define XY_SPAN_CDECL __cdecl
#else
#define XY_SPAN_CDECL
#endif
extern "C" {
void* XY_SPAN_CDECL xy_vsfilter_span_merge(const uint64_t*,size_t,const uint64_t*,size_t,uint64_t,uint64_t);
uint64_t XY_SPAN_CDECL xy_vsfilter_span_field(const void*,uint32_t,size_t);
void XY_SPAN_CDECL xy_vsfilter_span_release(void*);
}
#undef XY_SPAN_CDECL
namespace XyRust {
class SpanMerge {
    void* owner;
    SpanMerge(const SpanMerge&);SpanMerge& operator=(const SpanMerge&);
public:
    template<class Spans> SpanMerge(const Spans& a,const Spans& b,uint64_t offset1,uint64_t offset2):owner(NULL){
        if(a.size()>SIZE_MAX/2 || b.size()>SIZE_MAX/2)return;
        std::vector<uint64_t> left(a.size()*2),right(b.size()*2);
        for(size_t i=0;i<a.size();++i){left[2*i]=a[i].first;left[2*i+1]=a[i].second;}
        for(size_t i=0;i<b.size();++i){right[2*i]=b[i].first;right[2*i+1]=b[i].second;}
        owner=xy_vsfilter_span_merge(left.data(),a.size(),right.data(),b.size(),offset1,offset2);
    }
    ~SpanMerge(){xy_vsfilter_span_release(owner);}
    bool Ready()const{return owner!=NULL;}
    size_t Count()const{return static_cast<size_t>(xy_vsfilter_span_field(owner,0,0));}
    uint64_t Start(size_t i)const{return xy_vsfilter_span_field(owner,1,i);}
    uint64_t End(size_t i)const{return xy_vsfilter_span_field(owner,2,i);}
};
}
