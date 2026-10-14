// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>
#ifdef _WIN32
#include <atlstr.h>
#else
typedef uint8_t BYTE;
typedef wchar_t WCHAR;
#define PT_MOVETO 6
#define PT_LINETO 2
#define PT_BEZIERTO 4
#endif
#define PT_MOVETONC 0xfe
#define PT_BSPLINETO 0xfc
#define PT_BSPLINEPATCHTO 0xfa
#include "xy_rust_vobsub.h"
// Coordinates are restricted to Windows LONG values. long retains the actual
// %ld varargs type on portable platforms, where it can be wider than LONG.
struct CPoint {long x,y;CPoint(long a=0,long b=0):x(a),y(b){}};
template<class T> struct CAtlArray {
    std::vector<T> values;
    size_t GetCount()const{return values.size();}
    const T& operator[](size_t i)const{if(i>=values.size()){fprintf(stderr,"Path fixture left allocated storage\n");std::abort();}return values[i];}
};
struct PathInterrupted {};
struct PathString;
static PathString* pathOutput=nullptr;
static CAtlArray<BYTE>* pathTypes=nullptr;
static CAtlArray<CPoint>* pathPoints=nullptr;
static std::vector<BYTE> pathInputTypes;
static std::vector<CPoint> pathInputPoints;
static std::vector<std::wstring> pathEvents;
static int pathFailure=-1,pathAttempts=0;
static bool pathPolygonResult=true,pathMutate=false,pathMutated=false;
static uint64_t pathCommands=0;
static void observePath(const std::wstring& action);
struct PathString {
    std::wstring value;
    template<class... T> void Format(const wchar_t* format,T... args){
        observePath(L"construct");if(pathAttempts++==pathFailure)throw PathInterrupted();
#ifdef _WIN32
        ATL::CStringW native;native.Format(format,args...);value.assign(native.GetString(),native.GetLength());
#else
        wchar_t buffer[512];int n=std::swprintf(buffer,512,format,args...);if(n<0 || n>=512)std::abort();value.assign(buffer,n);
#endif
        observePath(std::wstring(L"formatted:")+value);
    }
    void SetString(const wchar_t* text,int length){
        observePath(L"construct");if(pathAttempts++==pathFailure)throw PathInterrupted();
#ifdef _WIN32
        ATL::CStringW native;native.SetString(text,length);value.assign(native.GetString(),native.GetLength());
#else
        // Widen the native UTF-16 payload for the portable string model.
        const auto* units=reinterpret_cast<const uint16_t*>(text);value.assign(units,units+length);
#endif
        observePath(std::wstring(L"formatted:")+value);
    }
    PathString& operator+=(const wchar_t* text){
        observePath(std::wstring(L"append:")+text);if(pathAttempts++==pathFailure)throw PathInterrupted();
#ifdef _WIN32
        ATL::CStringW native(value.c_str());native+=text;value.assign(native.GetString(),native.GetLength());
#else
        value+=text;
#endif
        if(pathMutate && !pathMutated && std::wcslen(text)==2 && text[1]==L' '){
            for(size_t i=0;i<pathTypes->values.size();++i)pathTypes->values[i]=i%2?PT_LINETO:PT_MOVETO;
            for(auto& p:pathPoints->values){p.x+=3;p.y-=4;}
            pathMutated=true;
        }
        observePath(std::wstring(L"appended:")+value);return *this;
    }
    PathString& operator+=(const PathString& other){return *this+=other.value.c_str();}
};
static void observePath(const std::wstring& action){
    pathEvents.push_back(action);pathEvents.push_back(pathOutput->value);
    std::wstring state;
    for(auto type:pathTypes->values)state+=std::to_wstring(type)+L",";
    state+=L"|";for(auto p:pathPoints->values)state+=std::to_wstring(p.x)+L","+std::to_wstring(p.y)+L";";
    pathEvents.push_back(state);
}
#define CStringW PathString
#define VOBSUB_PATH_SHELL(Name) \
class Name {public:struct {int left,top;}rect; \
    bool Polygonize(CStringW&,bool,int); \
    bool Polygonize(CAtlArray<BYTE>& types,CAtlArray<CPoint>& points,bool smooth,int scale){ \
        types.values=pathInputTypes;points.values=pathInputPoints;pathTypes=&types;pathPoints=&points; \
        observePath(L"polygon:"+std::to_wstring(smooth)+L","+std::to_wstring(scale));return pathPolygonResult;} \
};
static uint32_t observedPath(uint8_t kind,uint8_t previous,size_t remaining){++pathCommands;return xy_vsfilter_vobsub_path(kind,previous,remaining);}
#define xy_vsfilter_vobsub_path observedPath
