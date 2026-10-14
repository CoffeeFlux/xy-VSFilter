// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_SUBRIP_CUE_CDECL __cdecl
#else
#define XY_SUBRIP_CUE_CDECL
#endif
extern "C" {
void* XY_SUBRIP_CUE_CDECL xy_vsfilter_subrip_cue_create();
void XY_SUBRIP_CUE_CDECL xy_vsfilter_subrip_cue_empty_line(void*);
uint32_t XY_SUBRIP_CUE_CDECL xy_vsfilter_subrip_cue_boundary(const void*,uint32_t);
void XY_SUBRIP_CUE_CDECL xy_vsfilter_subrip_cue_append(void*,const uint16_t*,uint32_t);
uint32_t XY_SUBRIP_CUE_CDECL xy_vsfilter_subrip_cue_field(const void*,uint32_t,uint32_t);
void XY_SUBRIP_CUE_CDECL xy_vsfilter_subrip_cue_release(void*);
}
#undef XY_SUBRIP_CUE_CDECL
namespace XyRust {
class SubRipCue {
    void* owner;
    SubRipCue(const SubRipCue&);
    SubRipCue& operator=(const SubRipCue&);
public:
    SubRipCue():owner(xy_vsfilter_subrip_cue_create()){}
    ~SubRipCue(){xy_vsfilter_subrip_cue_release(owner);}
    void EmptyLine(){xy_vsfilter_subrip_cue_empty_line(owner);}
    bool Boundary(bool numeric)const{return xy_vsfilter_subrip_cue_boundary(owner,numeric?1:0)!=0;}
    template<class String> void Append(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_subrip_cue_append(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_subrip_cue_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_subrip_cue_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
