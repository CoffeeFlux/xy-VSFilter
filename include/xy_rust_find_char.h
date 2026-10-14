// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_FIND_CHAR_CDECL __cdecl
#else
#define XY_FIND_CHAR_CDECL
#endif
extern "C" {
void* XY_FIND_CHAR_CDECL xy_vsfilter_find_char_create(uint16_t, int32_t, uint32_t);
uint32_t XY_FIND_CHAR_CDECL xy_vsfilter_find_char_step(void*, const uint16_t*, uint32_t, uint32_t);
int32_t XY_FIND_CHAR_CDECL xy_vsfilter_find_char_value(const void*);
void XY_FIND_CHAR_CDECL xy_vsfilter_find_char_release(void*);
}
#undef XY_FIND_CHAR_CDECL
namespace XyRust {
class FindChar {
    void* owner;
    FindChar(const FindChar&);
    FindChar& operator=(const FindChar&);
public:
    FindChar(uint16_t target,int position,int length):owner(xy_vsfilter_find_char_create(target,position,static_cast<uint32_t>(length))){}
    ~FindChar(){xy_vsfilter_find_char_release(owner);}
    template<class String> uint32_t Step(const String& input,bool reply){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        return xy_vsfilter_find_char_step(owner,units.data(),static_cast<uint32_t>(units.size()),reply?1:0);
    }
    int Value()const{return xy_vsfilter_find_char_value(owner);}
};
}
