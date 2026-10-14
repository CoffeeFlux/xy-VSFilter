// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef _MSC_VER
#define XY_WRAP_CDECL __cdecl
#else
#define XY_WRAP_CDECL
#endif
extern "C" {
uint32_t XY_WRAP_CDECL xy_vsfilter_wrap_begin(int32_t,int32_t,int32_t*);
uint32_t XY_WRAP_CDECL xy_vsfilter_wrap_needs_word(const int32_t*);
uint32_t XY_WRAP_CDECL xy_vsfilter_wrap_push(int32_t*,int32_t);
uint32_t XY_WRAP_CDECL xy_vsfilter_wrap_finish(const int32_t*,int32_t,int32_t,int32_t*);
}
#undef XY_WRAP_CDECL
namespace XyRust {
class WrapWidth {
    int32_t state[4];
    bool valid;
public:
    WrapWidth(int full,int maximum):valid(xy_vsfilter_wrap_begin(full,maximum,state)!=0){}
    bool Valid()const{return valid;}
    bool NeedsWord()const{return valid && xy_vsfilter_wrap_needs_word(state)!=0;}
    bool Push(int width){valid=xy_vsfilter_wrap_push(state,width)!=0;return valid;}
    bool Finish(int full,int style,int& output)const{
        int32_t result;
        if(!valid || !xy_vsfilter_wrap_finish(state,full,style,&result))return false;
        output=result;return true;
    }
};
}
