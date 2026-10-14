// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cerrno>
#include "xy_rust_layout.h"
#ifdef _WIN32
#include <atlbase.h>
#include <atlcoll.h>
#include <atltypes.h>
using ATL::CAtlArray;
using ATL::CAtlList;
#else
struct CRect {
    int32_t left,top,right,bottom;
    CRect():left(0),top(0),right(0),bottom(0){}
    CRect(int l,int t,int r,int b):left(l),top(t),right(r),bottom(b){}
    int Height()const{return bottom-top;}
    bool IsRectEmpty()const{return left>=right || top>=bottom;}
    CRect operator+(const CRect& r)const{return CRect(left-r.left,top-r.top,right+r.right,bottom+r.bottom);}
    CRect operator&(const CRect& r)const{
        CRect result(std::max(left,r.left),std::max(top,r.top),std::min(right,r.right),std::min(bottom,r.bottom));
        return result.IsRectEmpty()?CRect():result;
    }
};
typedef void* POSITION;
template<class T> class CAtlArray {
    std::vector<T> values;
public:
    size_t GetCount()const{return values.size();}
    const T& operator[](size_t i)const{return values.at(i);}
    void Add(const T& value){values.push_back(value);}
};
template<class T> class CAtlList {
    struct Node {T value;Node* prev;Node* next;Node(const T& v,Node* p):value(v),prev(p),next(NULL){}};
    Node* head=NULL;Node* tail=NULL;size_t count=0;
    CAtlList(const CAtlList&);CAtlList& operator=(const CAtlList&);
public:
    CAtlList(){}~CAtlList(){RemoveAll();}
    size_t GetCount()const{return count;}
    POSITION GetHeadPosition()const{return head;}
    T& GetNext(POSITION& position){Node* node=static_cast<Node*>(position);position=node->next;return node->value;}
    void AddTail(const T& value){Node* node=new Node(value,tail);if(tail)tail->next=node;else head=node;tail=node;++count;}
    void RemoveAt(POSITION position){Node* node=static_cast<Node*>(position);if(node->prev)node->prev->next=node->next;else head=node->next;if(node->next)node->next->prev=node->prev;else tail=node->prev;delete node;--count;}
    void RemoveAll(){while(head)RemoveAt(head);}
};
#endif
struct CSubtitle {CRect m_rect;int m_topborder,m_bottomborder,m_scrAlignment;};
#define TRACE_RENDERER_REQUEST(message) ((void)0)
static uint64_t layoutOwners=0,layoutAdds=0,layoutRemoves=0,layoutUpdates=0,layoutFinds=0,layoutClears=0,layoutAdvanceHandled=0,layoutAdvanceFallback=0,layoutPlaceHandled=0,layoutPlaceFallback=0;
namespace XyRust {
class ObservedLayoutRects : public LayoutRects {
public:
    ObservedLayoutRects(){++layoutOwners;}
    template<class Array> bool Advance(int segment,const Array& active){bool handled=LayoutRects::Advance(segment,active);if(handled)++layoutAdvanceHandled;else ++layoutAdvanceFallback;return handled;}
    template<class Rect> bool Place(Rect& rect,int layer,bool down)const{bool handled=LayoutRects::Place(rect,layer,down);if(handled)++layoutPlaceHandled;else ++layoutPlaceFallback;return handled;}
    template<class Record> void AddTail(const Record& r){++layoutAdds;LayoutRects::AddTail(r);}
    void Remove(size_t i){++layoutRemoves;LayoutRects::Remove(i);}
    void Segment(size_t i,int segment){++layoutUpdates;LayoutRects::Segment(i,segment);}
    size_t Find(int segment,int entry)const{++layoutFinds;return LayoutRects::Find(segment,entry);}
    void RemoveAll(){++layoutClears;LayoutRects::RemoveAll();}
};
}
