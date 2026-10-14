// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "microdvd_shell.h"
#include "xy_rust_fields.h"
#include "xy_rust_ass_style.h"
#include <cerrno>
#include <cfenv>
#include <new>
#ifndef _WIN32
typedef uint32_t COLORREF;
enum {DEFAULT_CHARSET=1,FW_BOLD=700,FW_NORMAL=400};
#endif
struct StyleRect {
    int32_t left,top,right,bottom;
    StyleRect():left(0),top(0),right(0),bottom(0){}
    StyleRect(int a,int b,int c,int d):left(a),top(b),right(c),bottom(d){}
};
static int liveStyles=0;
struct STSStyle {
    int charSet;MicroString fontName;double fontSize;int fontWeight;
    bool fItalic,fUnderline,fStrikeOut;
    StyleRect marginRect;int scrAlignment,borderStyle;
    double outlineWidthX,outlineWidthY,shadowDepthX,shadowDepthY;
    COLORREF colors[4];BYTE alpha[4];
    double fontScaleX,fontScaleY,fontSpacing,fBlur,fGaussianBlur;
    double fontAngleZ,fontAngleX,fontAngleY,fontShiftX,fontShiftY;
    int relativeTo;
    STSStyle();void SetDefault();
    static void* operator new(size_t n){void* p=::operator new(n);++liveStyles;return p;}
    static void operator delete(void* p){--liveStyles;::operator delete(p);}
};
static std::vector<uint64_t> styleSnapshot(const STSStyle& s){
    std::vector<uint64_t> v;
    for(int i:{s.charSet,s.fontWeight,s.fItalic?1:0,s.fUnderline?1:0,s.fStrikeOut?1:0,s.marginRect.left,s.marginRect.top,s.marginRect.right,s.marginRect.bottom,s.scrAlignment,s.borderStyle,s.relativeTo})v.push_back(static_cast<uint32_t>(i));
    for(int i=0;i<4;++i){v.push_back(s.colors[i]);v.push_back(s.alpha[i]);}
    const double numbers[]={s.fontSize,s.outlineWidthX,s.outlineWidthY,s.shadowDepthX,s.shadowDepthY,s.fontScaleX,s.fontScaleY,s.fontSpacing,s.fBlur,s.fGaussianBlur,s.fontAngleZ,s.fontAngleX,s.fontAngleY,s.fontShiftX,s.fontShiftY};
    for(const double& d:numbers){uint64_t bits;std::memcpy(&bits,&d,8);v.push_back(bits);}
    v.push_back(s.fontName.GetLength());for(int i=0;i<s.fontName.GetLength();++i)v.push_back(static_cast<uint16_t>(s.fontName[i]));
    return v;
}
struct CSimpleTextSubtitle {
    std::vector<std::vector<uint64_t>> entries;
    void AddStyle(const MicroString& name,STSStyle* style){
        microObserve(35,name.GetString());
        auto entry=styleSnapshot(*style);entry.push_back(name.GetLength());
        for(int i=0;i<name.GetLength();++i)entry.push_back(static_cast<uint16_t>(name[i]));
        entries.push_back(entry);delete style;
    }
};
static uint64_t styleOwners=0,styleSteps=0,styleAccepts=0,styleNormalizations=0;
namespace XyRust {
class ObservedAssStyle : public AssStyle {
public:
    template<class String> ObservedAssStyle(const String& input,int version):AssStyle(input,version){++styleOwners;}
    uint32_t Next(){++styleSteps;return AssStyle::Next();}
    template<class String> void Accept(const String& input){++styleAccepts;AssStyle::Accept(input);}
    void Normalize(const uint32_t* colors,int alpha,int encoding,int border,int alignment){++styleNormalizations;AssStyle::Normalize(colors,alpha,encoding,border,alignment);}
};
}
static int styleScan(const wchar_t* text,const wchar_t* format,int* value){
    microObserve(36,text);searchEvents.push_back(format[1]);
    const int result=std::swscanf(text,format,value);
    searchEvents.push_back(static_cast<uint32_t>(result));if(result==1)searchEvents.push_back(static_cast<uint32_t>(*value));
    searchEvents.push_back(errno);return result;
}
static int styleScan(const wchar_t* text,const wchar_t* format,float* value){
    microObserve(37,text);searchEvents.push_back(format[1]);
    const int result=std::swscanf(text,format,value);
    searchEvents.push_back(static_cast<uint32_t>(result));if(result==1){uint32_t bits;std::memcpy(&bits,value,4);searchEvents.push_back(bits);}
    searchEvents.push_back(errno);return result;
}
#define swscanf styleScan
#define CString MicroString
#define CRect StyleRect
#define DEBUG_NEW new
#ifndef _T
#define _T(text) L##text
#endif
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))
