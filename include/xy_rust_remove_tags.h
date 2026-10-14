// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_REMOVE_TAGS_CDECL __cdecl
#else
#define XY_REMOVE_TAGS_CDECL
#endif
extern "C" {
void* XY_REMOVE_TAGS_CDECL xy_vsfilter_remove_tags_create(const uint16_t*,uint32_t);
uint32_t XY_REMOVE_TAGS_CDECL xy_vsfilter_remove_tags_step(void*,int32_t);
int32_t XY_REMOVE_TAGS_CDECL xy_vsfilter_remove_tags_field(const void*,uint32_t,uint32_t);
void XY_REMOVE_TAGS_CDECL xy_vsfilter_remove_tags_release(void*);
}
#undef XY_REMOVE_TAGS_CDECL
namespace XyRust {
class RemoveTags {
    void* owner;
    RemoveTags(const RemoveTags&);
    RemoveTags& operator=(const RemoveTags&);
public:
    template<class String> explicit RemoveTags(const String& input):owner(NULL){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_remove_tags_create(units.data(),static_cast<uint32_t>(units.size()));
    }
    ~RemoveTags(){xy_vsfilter_remove_tags_release(owner);}
    bool Handled()const{return owner!=NULL;}
    unsigned Step(int reply){return xy_vsfilter_remove_tags_step(owner,reply);}
    int Position()const{return xy_vsfilter_remove_tags_field(owner,0,0);}
    template<class String> String Text()const{
        const int count=xy_vsfilter_remove_tags_field(owner,1,0);
        std::vector<wchar_t> units(static_cast<size_t>(count)+1,0);
        for(int i=0;i<count;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_remove_tags_field(owner,2,static_cast<uint32_t>(i)));
        return String(units.data(),count);
    }
};
}
