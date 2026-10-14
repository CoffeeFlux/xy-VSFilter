// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_SSF_CDECL __cdecl
#else
#define XY_SSF_CDECL
#endif
extern "C" {
void* XY_SSF_CDECL xy_vsfilter_ssf_queue_create();
void XY_SSF_CDECL xy_vsfilter_ssf_queue_push(void*,int32_t);
int32_t XY_SSF_CDECL xy_vsfilter_ssf_queue_pop(void*);
size_t XY_SSF_CDECL xy_vsfilter_ssf_queue_count(const void*);
int32_t XY_SSF_CDECL xy_vsfilter_ssf_queue_end(const void*,uint32_t);
void XY_SSF_CDECL xy_vsfilter_ssf_queue_release(void*);
}
#undef XY_SSF_CDECL
namespace XyRust {
class SsfLookahead {
    void* owner;
    SsfLookahead(const SsfLookahead&);
    SsfLookahead& operator=(const SsfLookahead&);
public:
    SsfLookahead():owner(xy_vsfilter_ssf_queue_create()){}
    ~SsfLookahead(){xy_vsfilter_ssf_queue_release(owner);}
    size_t GetCount()const{return xy_vsfilter_ssf_queue_count(owner);}
    bool IsEmpty()const{return GetCount()==0;}
    int GetHead()const{return xy_vsfilter_ssf_queue_end(owner,0);}
    int GetTail()const{return xy_vsfilter_ssf_queue_end(owner,1);}
    void AddTail(int value){xy_vsfilter_ssf_queue_push(owner,value);}
    int RemoveHead(){return xy_vsfilter_ssf_queue_pop(owner);}
};
}
