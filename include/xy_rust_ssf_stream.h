// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_SSF_CDECL __cdecl
#else
#define XY_SSF_CDECL
#endif
struct XYRustSsfPeek {uint32_t phase;int32_t previous,result;};
extern "C" {
void* XY_SSF_CDECL xy_vsfilter_ssf_wide_create(const uint16_t*,int32_t);
int32_t XY_SSF_CDECL xy_vsfilter_ssf_wide_read(const void*,int32_t);
void XY_SSF_CDECL xy_vsfilter_ssf_wide_release(void*);
void* XY_SSF_CDECL xy_vsfilter_ssf_memory_create(const uint8_t*,int32_t);
const uint8_t* XY_SSF_CDECL xy_vsfilter_ssf_memory_data(const void*);
int32_t XY_SSF_CDECL xy_vsfilter_ssf_memory_read(const void*,int32_t);
void XY_SSF_CDECL xy_vsfilter_ssf_memory_release(void*);
uint64_t XY_SSF_CDECL xy_vsfilter_ssf_output(uint16_t,uint32_t);
uint32_t XY_SSF_CDECL xy_vsfilter_ssf_peek_step(XYRustSsfPeek*,size_t,int32_t,int32_t,int32_t);
void* XY_SSF_CDECL xy_vsfilter_ssf_queue_create();
void XY_SSF_CDECL xy_vsfilter_ssf_queue_push(void*,int32_t);
int32_t XY_SSF_CDECL xy_vsfilter_ssf_queue_pop(void*);
size_t XY_SSF_CDECL xy_vsfilter_ssf_queue_count(const void*);
int32_t XY_SSF_CDECL xy_vsfilter_ssf_queue_end(const void*,uint32_t);
void XY_SSF_CDECL xy_vsfilter_ssf_queue_release(void*);
}
#undef XY_SSF_CDECL
namespace XyRust {
// Retain the CStringW reference so caller-held pointers keep the original COW
// lifetime. Rust owns the snapshot used for reads; CStringW is also fallback.
class SsfWide {
    void* owner;
    int length;
    CStringW fallback;
    SsfWide(const SsfWide&);
    SsfWide& operator=(const SsfWide&);
public:
    SsfWide(const CStringW& input):owner(NULL),length(input.GetLength()),fallback(input){
        owner=xy_vsfilter_ssf_wide_create(reinterpret_cast<const uint16_t*>(input.GetString()),length);
    }
    ~SsfWide(){xy_vsfilter_ssf_wide_release(owner);}
    int GetLength()const{return length;}
    WCHAR operator[](int position)const{return owner?static_cast<WCHAR>(xy_vsfilter_ssf_wide_read(owner,position)):fallback[position];}
};
class SsfMemory {
    void* owner;
    SsfMemory(const SsfMemory&);
    SsfMemory& operator=(const SsfMemory&);
public:
    SsfMemory():owner(NULL){}
    ~SsfMemory(){Reset();}
    bool Assign(const uint8_t* input,int length){Reset();owner=xy_vsfilter_ssf_memory_create(input,length);return Ready();}
    bool Ready()const{return owner!=NULL;}
    const uint8_t* Data()const{return xy_vsfilter_ssf_memory_data(owner);}
    int Read(int position)const{return xy_vsfilter_ssf_memory_read(owner,position);}
    void Reset(){xy_vsfilter_ssf_memory_release(owner);owner=NULL;}
};
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
class SsfPeek {
    XYRustSsfPeek state;
public:
    enum {Read=0,Pop=1,Done=2,Check=3};
    SsfPeek(){state.phase=0;state.previous=0;state.result=0;}
    uint32_t Next(const SsfLookahead& queue,int last){
        const size_t count=queue.GetCount();
        return xy_vsfilter_ssf_peek_step(&state,count,count?queue.GetHead():0,count?queue.GetTail():0,last);
    }
    int Value()const{return state.result;}
};

}
