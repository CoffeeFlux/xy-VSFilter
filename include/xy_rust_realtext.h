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
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_create(uint32_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_next(void*, const uint16_t*, size_t, uint32_t, uint32_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_release(void*);
}
#undef XY_REALTEXT_CDECL
namespace XyRust {
class RealTextAttributes {
    void* owner;
    RealTextAttributes(const RealTextAttributes&);
    RealTextAttributes& operator=(const RealTextAttributes&);
    std::wstring String(uint32_t field)const{
        const size_t length=xy_vsfilter_realtext_attributes_field(owner,field,0);
        std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_attributes_field(owner,field+1,i));
        return result;
    }
public:
    explicit RealTextAttributes(uint32_t position):owner(xy_vsfilter_realtext_attributes_create(position)){}
    ~RealTextAttributes(){xy_vsfilter_realtext_attributes_release(owner);}
    uint32_t Next(const std::wstring& input,bool ignore,bool response){
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_attributes_next(owner,reinterpret_cast<const uint16_t*>(input.data()),input.size(),ignore?1:0,response?1:0);
#else
        const std::vector<uint16_t> units(input.begin(),input.end());
        return xy_vsfilter_realtext_attributes_next(owner,units.data(),units.size(),ignore?1:0,response?1:0);
#endif
    }
    uint32_t Position()const{return static_cast<uint32_t>(xy_vsfilter_realtext_attributes_field(owner,0,0));}
    wchar_t Character()const{return static_cast<wchar_t>(xy_vsfilter_realtext_attributes_field(owner,1,0));}
    std::wstring Name()const{return String(2);}
    std::wstring Value()const{return String(4);}
};
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
