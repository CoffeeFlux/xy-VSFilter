// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_ASS_DIALOGUE_CDECL __cdecl
#else
#define XY_ASS_DIALOGUE_CDECL
#endif
extern "C" {
void* XY_ASS_DIALOGUE_CDECL xy_vsfilter_ass_dialogue_create(const uint16_t*,uint32_t,int32_t);
uint32_t XY_ASS_DIALOGUE_CDECL xy_vsfilter_ass_dialogue_step(void*,int32_t);
int32_t XY_ASS_DIALOGUE_CDECL xy_vsfilter_ass_dialogue_number(const void*,uint32_t);
uint32_t XY_ASS_DIALOGUE_CDECL xy_vsfilter_ass_dialogue_field(const void*,uint32_t,uint32_t,uint32_t);
void XY_ASS_DIALOGUE_CDECL xy_vsfilter_ass_dialogue_release(void*);
}
#undef XY_ASS_DIALOGUE_CDECL
namespace XyRust {
class AssDialogue {
    void* owner;
    AssDialogue(const AssDialogue&);
    AssDialogue& operator=(const AssDialogue&);
public:
    template<class String> AssDialogue(const String& input,int version){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_ass_dialogue_create(units.data(),static_cast<uint32_t>(units.size()),version);
    }
    ~AssDialogue(){xy_vsfilter_ass_dialogue_release(owner);}
    uint32_t Step(int reply){return xy_vsfilter_ass_dialogue_step(owner,reply);}
    int Number(uint32_t index)const{return xy_vsfilter_ass_dialogue_number(owner,index);}
    wchar_t Separator()const{return static_cast<wchar_t>(xy_vsfilter_ass_dialogue_field(owner,0,2,0));}
    template<class String> String Text(uint32_t group)const{
        const uint32_t length=xy_vsfilter_ass_dialogue_field(owner,group,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_ass_dialogue_field(owner,group,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
