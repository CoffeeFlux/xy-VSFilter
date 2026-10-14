// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include "xy_rust_vobsub.h"
typedef uint8_t BYTE;
struct RGBQUAD { BYTE rgbBlue,rgbGreen,rgbRed,rgbReserved; };
struct CPoint { int32_t x,y;CPoint(int32_t a=0,int32_t b=0):x(a),y(b){} };
struct CRect { int32_t left,top,right,bottom;CRect(int32_t l,int32_t t,int32_t r,int32_t b):left(l),top(t),right(r),bottom(b){} CRect():left(0),top(0),right(0),bottom(0){} int32_t Width()const{return right-left;} int32_t Height()const{return bottom-top;} };
static std::vector<uint64_t> outlineEvents;
static BYTE* outlineMask=nullptr;
static size_t outlineMaskCount=0;
static CPoint* outlineTopLeft=nullptr;
static int outlineFailure=-1,outlineAllocations=0,outlineObservations=0,outlineRefusals=0,outlineAssertions=0;
static uint64_t maskHandled=0,findHandled=0,walkHandled=0,outlineFallback=0;
static void observeOutline(unsigned action,int x=0,int y=0,int d=0) {
    if(++outlineObservations>10000){fprintf(stderr,"Outline fixture exceeded its finite observation budget\n");std::abort();}
    outlineEvents.insert(outlineEvents.end(),{uint64_t(action),uint64_t(static_cast<uint32_t>(x)),uint64_t(static_cast<uint32_t>(y)),uint64_t(static_cast<uint32_t>(d)),uint64_t(static_cast<uint32_t>(outlineTopLeft->x)),uint64_t(static_cast<uint32_t>(outlineTopLeft->y)),uint64_t(outlineMaskCount)});
    for(size_t i=0;i<outlineMaskCount;i+=8) {
        uint64_t word=0;for(size_t j=0;j<8 && i+j<outlineMaskCount;++j)word|=uint64_t(outlineMask[i+j])<<(j*8);outlineEvents.push_back(word);
    }
}
struct CMemoryException {void Delete(){observeOutline(4);}};
static CMemoryException outlineException;
struct OutlineAllocation {};
inline void* operator new(size_t bytes,OutlineAllocation) {
    observeOutline(2,static_cast<int>(bytes));
    if(outlineAllocations++==outlineFailure){++outlineRefusals;throw &outlineException;}
    return ::operator new(bytes);
}
inline void operator delete(void* memory,OutlineAllocation) noexcept {::operator delete(memory);}
#define DEBUG_NEW new (OutlineAllocation{})
#define FALSE 0
#define ASSERT(condition) do {if(!(condition)){++outlineAssertions;observeOutline(5);}}while(0)
template<class T> class CAutoVectorPtr {
    T* pointer=nullptr;
public:
    bool Allocate(size_t n){observeOutline(1,static_cast<int>(n));if(outlineAllocations++==outlineFailure){++outlineRefusals;return false;}pointer=new T[n];std::fill(pointer,pointer+n,T(0xa5));outlineMask=pointer;outlineMaskCount=n;return true;}
    ~CAutoVectorPtr(){if(pointer){observeOutline(7);outlineMask=nullptr;outlineMaskCount=0;delete[] pointer;}}
    operator T*(){return pointer;}
    T& operator[](int i){if(i<0 || static_cast<size_t>(i)>=outlineMaskCount){fprintf(stderr,"Outline fixture left its allocated mask\n");std::abort();}return pointer[i];}
};
template<class T> class CAutoPtr {
    T* pointer=nullptr;
public:
    ~CAutoPtr(){delete pointer;}
    void Attach(T* p){pointer=p;}
    T* Detach(){T* p=pointer;pointer=nullptr;return p;}
    T* operator->(){return pointer;}
};
template<class T> struct OutlineArray {
    std::vector<T> values;
    bool IsEmpty()const{return values.empty();}
    void Add(const T& value){values.push_back(value);}
};
struct COutline {
    OutlineArray<CPoint> pa;OutlineArray<int> da;
    void Add(CPoint point,int direction){observeOutline(3,point.x,point.y,direction);pa.Add(point);da.Add(direction);}
};
template<class T> struct CAutoPtrList {
    std::vector<T*> values;
    ~CAutoPtrList(){for(auto* value:values)delete value;}
    void AddTail(CAutoPtr<T>& value){observeOutline(6);values.push_back(value.Detach());}
};
#define GP(xx, yy) (((xx) < 0 || (yy) < 0 || (xx) >= w || (yy) >= h) ? 0 : p[(yy) * w + (xx)])
#define VOBSUB_OUTLINE_SHELL(Name) \
class Name { public: CRect rect;RGBQUAD* lpPixels;CAutoPtrList<COutline>* GetOutlineList(CPoint&); };
static uint32_t observedMask(const uint8_t* source,uint8_t* output,size_t count){const auto r=xy_vsfilter_vobsub_outline_mask(source,output,count);maskHandled+=r!=0;outlineFallback+=r==0;return r;}
static uint32_t observedFind(const uint8_t* grid,int32_t width,int32_t height,int32_t* point){const auto r=xy_vsfilter_vobsub_outline_find(grid,width,height,point);findHandled+=r!=0;outlineFallback+=r==0;return r;}
static uint32_t observedWalk(uint8_t* grid,int32_t width,int32_t height,int32_t x,int32_t y,int32_t direction,XyVobSubOutlineStep* output){const auto r=xy_vsfilter_vobsub_outline_walk(grid,width,height,x,y,direction,output);walkHandled+=r!=0;outlineFallback+=r==0;return r;}
#define xy_vsfilter_vobsub_outline_mask observedMask
#define xy_vsfilter_vobsub_outline_find observedFind
#define xy_vsfilter_vobsub_outline_walk observedWalk
