// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_PIPE_LINE_CDECL __cdecl
#else
#define XY_PIPE_LINE_CDECL
#endif
extern "C" {
void* XY_PIPE_LINE_CDECL xy_vsfilter_pipe_line_create(const uint16_t*,uint32_t);
int32_t XY_PIPE_LINE_CDECL xy_vsfilter_pipe_line_position(const void*);
void XY_PIPE_LINE_CDECL xy_vsfilter_pipe_line_found(void*,int32_t);
uint32_t XY_PIPE_LINE_CDECL xy_vsfilter_pipe_line_field(const void*,uint32_t,uint32_t);
void XY_PIPE_LINE_CDECL xy_vsfilter_pipe_line_release(void*);
}
#undef XY_PIPE_LINE_CDECL
namespace XyRust {
class PipeLine {
    void* owner;
    PipeLine(const PipeLine&);
    PipeLine& operator=(const PipeLine&);
public:
    template<class String> explicit PipeLine(const String& input):owner(NULL){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_pipe_line_create(units.data(),static_cast<uint32_t>(units.size()));
    }
    ~PipeLine(){xy_vsfilter_pipe_line_release(owner);}
    int Position()const{return xy_vsfilter_pipe_line_position(owner);}
    void Found(int found){xy_vsfilter_pipe_line_found(owner,found);}
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_pipe_line_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_pipe_line_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
