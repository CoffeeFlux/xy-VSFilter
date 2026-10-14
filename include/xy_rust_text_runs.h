// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_TEXT_RUNS_CDECL __cdecl
#else
#define XY_TEXT_RUNS_CDECL
#endif
extern "C" {
void* XY_TEXT_RUNS_CDECL xy_vsfilter_text_runs_create(const uint16_t*,uint32_t);
uint32_t XY_TEXT_RUNS_CDECL xy_vsfilter_text_runs_prepare(void*,int32_t);
void XY_TEXT_RUNS_CDECL xy_vsfilter_text_runs_reset(void*,const uint16_t*,uint32_t);
uint32_t XY_TEXT_RUNS_CDECL xy_vsfilter_text_runs_next(void*);
uint32_t XY_TEXT_RUNS_CDECL xy_vsfilter_text_runs_field(const void*,uint32_t,uint32_t);
void XY_TEXT_RUNS_CDECL xy_vsfilter_text_runs_release(void*);
}
#undef XY_TEXT_RUNS_CDECL
namespace XyRust {
class TextRuns {
    void* owner;
    TextRuns(const TextRuns&);
    TextRuns& operator=(const TextRuns&);
public:
    template<class String> explicit TextRuns(const String& input):owner(NULL){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_text_runs_create(units.data(),static_cast<uint32_t>(units.size()));
    }
    ~TextRuns(){xy_vsfilter_text_runs_release(owner);}
    bool Prepare(int wrap){return xy_vsfilter_text_runs_prepare(owner,wrap)!=0;}
    template<class String> void Reset(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_text_runs_reset(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    uint32_t Next(){return xy_vsfilter_text_runs_next(owner);}
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_text_runs_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_text_runs_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
