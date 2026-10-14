// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cerrno>
#include "xy_rust_line_layout.h"
#ifdef _WIN32
#include <atlbase.h>
#include <atlcoll.h>
#include <atltypes.h>
#else
typedef void* POSITION;
struct CSize {int cx,cy;CSize(int x=0,int y=0):cx(x),cy(y){}};
struct CPoint {int x,y;CPoint(int a,int b):x(a),y(b){}};
struct CRect {
    int left,top,right,bottom;
    CRect(int l=0,int t=0,int r=0,int b=0):left(l),top(t),right(r),bottom(b){}
    CRect(CPoint point,CSize size):left(point.x),top(point.y),right(point.x+size.cx),bottom(point.y+size.cy){}
};
using std::max;
#endif
struct CLine {int m_width,m_ascent,m_descent,m_borderX,m_borderY,id;};
struct LineWords {std::vector<CLine> lines;POSITION GetHeadPosition(){return lines.empty()?NULL:reinterpret_cast<POSITION>(&lines[0]);}};
struct LineInterrupted {};
static std::vector<int64_t> lineEvents;
static int lineFailure=-1,lineAttempts=0,lineMutation=0,lineMutationCount=0,lineStop=-1;
struct LineState {
    LineWords m_words;CRect m_rect;int m_topborder,m_bottomborder,m_scrAlignment;std::vector<int> output;
    LineState():m_rect(91,92,93,94),m_topborder(95),m_bottomborder(96),m_scrAlignment(2){}
    void event(int kind,int id,int width){
        lineEvents.insert(lineEvents.end(),{kind,id,width,m_topborder,m_bottomborder,m_scrAlignment,m_rect.left,m_rect.top,m_rect.right,m_rect.bottom,static_cast<int>(output.size())});
        if(lineAttempts++==lineFailure)throw LineInterrupted();
    }
    CLine* GetNextLine(POSITION& pos,int maximum){
        CLine* line=reinterpret_cast<CLine*>(pos);event(1,line->id,maximum);
        if(line->id==lineStop)return NULL;
        size_t index=static_cast<size_t>(line-&m_words.lines[0]);
        pos=index+1<m_words.lines.size()?reinterpret_cast<POSITION>(&m_words.lines[index+1]):NULL;
        if(lineMutation==1){line->m_width+=3;m_scrAlignment=(m_scrAlignment+1)%10;++lineMutationCount;}
        return line;
    }
    void AddTail(CLine* line){
        event(2,line->id,line->m_width);output.push_back(line->id);
        if(lineMutation==2){line->m_borderY+=5;line->m_ascent-=2;m_scrAlignment=(m_scrAlignment+1)%10;++lineMutationCount;}
    }
    std::vector<int64_t> snapshot()const{
        std::vector<int64_t> out={m_topborder,m_bottomborder,m_scrAlignment,m_rect.left,m_rect.top,m_rect.right,m_rect.bottom,static_cast<int>(output.size())};
        out.insert(out.end(),output.begin(),output.end());for(const auto& l:m_words.lines)out.insert(out.end(),{l.m_width,l.m_ascent,l.m_descent,l.m_borderX,l.m_borderY,l.id});return out;
    }
};
static uint64_t lineGrows=0,linePlaces=0,lineDeclines=0;
namespace XyRust {
// Different name in the extracted caller records bridge usage without changing it.
template<class Size> bool ObservedGrowLineSpace(Size& space,int width,int border,int ascent,int descent){bool ok=GrowLineSpace(space,width,border,ascent,descent);if(ok)++lineGrows;else ++lineDeclines;return ok;}
template<class Size,class Rect> bool ObservedPlaceLines(Rect& rect,const Size& size,const Rect& margin,const Size& space,int alignment){bool ok=PlaceLines(rect,size,margin,space,alignment);if(ok)++linePlaces;else ++lineDeclines;return ok;}
}
