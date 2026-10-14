// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#ifdef _MSC_VER
#define XY_REALTEXT_CDECL __cdecl
#else
#define XY_REALTEXT_CDECL
#endif
extern "C" {
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_create(uint32_t, int32_t, int32_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_begin(void*, const uint16_t*, size_t, uint32_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_attribute_kind(const void*, const uint16_t*, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_attribute(void*, const uint16_t*, size_t, const uint16_t*, size_t, uint32_t, int32_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_end(void*);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_release(void*);
}
#undef XY_REALTEXT_CDECL
namespace XyRust {
class RealTextTags {
    void* owner;
    RealTextTags(const RealTextTags&);
    RealTextTags& operator=(const RealTextTags&);
public:
    RealTextTags(uint32_t ignore,int minimum,int maximum):owner(xy_vsfilter_realtext_create(ignore,minimum,maximum)){}
    ~RealTextTags(){xy_vsfilter_realtext_release(owner);}
    bool Begin(const std::wstring& name,uint32_t flags){
        const std::vector<uint16_t> units(name.begin(),name.end());
        return xy_vsfilter_realtext_begin(owner,units.data(),units.size(),flags)!=0;
    }
    uint32_t AttributeKind(const std::wstring& name)const{
        const std::vector<uint16_t> units(name.begin(),name.end());
        return xy_vsfilter_realtext_attribute_kind(owner,units.data(),units.size());
    }
    void Attribute(const std::wstring& name,const std::wstring& value,bool numeric,int size){
        const std::vector<uint16_t> key(name.begin(),name.end()),text(value.begin(),value.end());
        xy_vsfilter_realtext_attribute(owner,key.data(),key.size(),text.data(),text.size(),numeric?1:0,size);
    }
    void End(){xy_vsfilter_realtext_end(owner);}
    std::wstring Result()const{
        const size_t length=xy_vsfilter_realtext_field(owner,0,0);
        std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_field(owner,1,i));
        return result;
    }
};
}
