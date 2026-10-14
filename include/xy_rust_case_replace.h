// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_CASE_REPLACE_CDECL __cdecl
#else
#define XY_CASE_REPLACE_CDECL
#endif
extern "C" {
void* XY_CASE_REPLACE_CDECL xy_vsfilter_case_replace_create(const uint16_t*,uint32_t,const uint16_t*,uint32_t,const uint16_t*,uint32_t,const uint16_t*,uint32_t);
uint32_t XY_CASE_REPLACE_CDECL xy_vsfilter_case_replace_field(const void*,uint32_t,uint32_t);
void XY_CASE_REPLACE_CDECL xy_vsfilter_case_replace_release(void*);
}
#undef XY_CASE_REPLACE_CDECL
namespace XyRust {
class CaseReplace {
    void* owner;
    CaseReplace(const CaseReplace&);
    CaseReplace& operator=(const CaseReplace&);
public:
    CaseReplace():owner(NULL){}
    ~CaseReplace(){xy_vsfilter_case_replace_release(owner);}
    template<class String> bool Convert(String& source,const String& lowered,const String& from,const String& to){
        const String* inputs[]={&source,&lowered,&from,&to};
        std::vector<uint16_t> units[4];
        for(int n=0;n<4;++n){
            units[n].resize(static_cast<size_t>(inputs[n]->GetLength()));
            for(int i=0;i<inputs[n]->GetLength();++i)units[n][i]=static_cast<uint16_t>((*inputs[n])[i]);
        }
        xy_vsfilter_case_replace_release(owner);
        owner=xy_vsfilter_case_replace_create(units[0].data(),static_cast<uint32_t>(units[0].size()),units[1].data(),static_cast<uint32_t>(units[1].size()),units[2].data(),static_cast<uint32_t>(units[2].size()),units[3].data(),static_cast<uint32_t>(units[3].size()));
        if(!owner)return false;
        const uint32_t count=xy_vsfilter_case_replace_field(owner,0,0);
        std::vector<wchar_t> output(static_cast<size_t>(count)+1,0);
        for(uint32_t i=0;i<count;++i)output[i]=static_cast<wchar_t>(xy_vsfilter_case_replace_field(owner,1,i));
        source=String(output.data(),static_cast<int>(count));return true;
    }
};
}
