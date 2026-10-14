// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_SAMI_CDECL __cdecl
#else
#define XY_SAMI_CDECL
#endif
extern "C" {
void* XY_SAMI_CDECL xy_vsfilter_sami_create(const uint16_t*,uint32_t,const uint16_t*,uint32_t);
uint32_t XY_SAMI_CDECL xy_vsfilter_sami_step(void*);
void XY_SAMI_CDECL xy_vsfilter_sami_insert(void*,const uint16_t*,uint32_t,uint32_t);
uint32_t XY_SAMI_CDECL xy_vsfilter_sami_field(const void*,uint32_t,uint32_t,uint32_t);
void XY_SAMI_CDECL xy_vsfilter_sami_release(void*);
void* XY_SAMI_CDECL xy_vsfilter_sami_arguments_create(const uint16_t*,uint32_t);
uint32_t XY_SAMI_CDECL xy_vsfilter_sami_arguments_accept(void*,const uint16_t*,uint32_t);
uint32_t XY_SAMI_CDECL xy_vsfilter_sami_arguments_field(const void*,uint32_t,uint32_t,uint32_t);
void XY_SAMI_CDECL xy_vsfilter_sami_arguments_release(void*);
}
#undef XY_SAMI_CDECL
namespace XyRust {
class Sami {
    void* owner;
    Sami(const Sami&);
    Sami& operator=(const Sami&);
public:
    template<class String> Sami(const String& source,const String& lowered):owner(NULL){
        std::vector<uint16_t> text(static_cast<size_t>(source.GetLength())),lower(static_cast<size_t>(lowered.GetLength()));
        for(int i=0;i<source.GetLength();++i)text[i]=static_cast<uint16_t>(source[i]);
        for(int i=0;i<lowered.GetLength();++i)lower[i]=static_cast<uint16_t>(lowered[i]);
        owner=xy_vsfilter_sami_create(text.data(),static_cast<uint32_t>(text.size()),lower.data(),static_cast<uint32_t>(lower.size()));
    }
    ~Sami(){xy_vsfilter_sami_release(owner);}
    bool Handled()const{return owner!=NULL;}
    unsigned Step(){return xy_vsfilter_sami_step(owner);}
    template<class String> String Text(unsigned group)const{
        const uint32_t count=xy_vsfilter_sami_field(owner,group,0,0);
        std::vector<wchar_t> text(static_cast<size_t>(count)+1,0);
        for(uint32_t i=0;i<count;++i)text[i]=static_cast<wchar_t>(xy_vsfilter_sami_field(owner,group,1,i));
        return String(text.data(),static_cast<int>(count));
    }
    template<class String> void Insert(const String& input,int advance){
        std::vector<uint16_t> text(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)text[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_sami_insert(owner,text.data(),static_cast<uint32_t>(text.size()),static_cast<uint32_t>(advance));
    }
};
class SamiArguments {
    void* owner;
    SamiArguments(const SamiArguments&);
    SamiArguments& operator=(const SamiArguments&);
public:
    template<class String> explicit SamiArguments(const String& input):owner(NULL){
        std::vector<uint16_t> text(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)text[i]=static_cast<uint16_t>(input[i]);
        owner=xy_vsfilter_sami_arguments_create(text.data(),static_cast<uint32_t>(text.size()));
    }
    ~SamiArguments(){xy_vsfilter_sami_arguments_release(owner);}
    template<class String> unsigned Accept(const String& input){
        std::vector<uint16_t> text(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)text[i]=static_cast<uint16_t>(input[i]);
        return xy_vsfilter_sami_arguments_accept(owner,text.data(),static_cast<uint32_t>(text.size()));
    }
    template<class String> String Text(unsigned group)const{
        const uint32_t count=xy_vsfilter_sami_arguments_field(owner,group,0,0);
        std::vector<wchar_t> text(static_cast<size_t>(count)+1,0);
        for(uint32_t i=0;i<count;++i)text[i]=static_cast<wchar_t>(xy_vsfilter_sami_arguments_field(owner,group,1,i));
        return String(text.data(),static_cast<int>(count));
    }
};

}
