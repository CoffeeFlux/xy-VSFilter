// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#ifdef _MSC_VER
#define XY_SAMI_FILE_CDECL __cdecl
#else
#define XY_SAMI_FILE_CDECL
#endif
extern "C" {
void* XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_create();
uint32_t XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_begin(void*,const uint16_t*,uint32_t);
void XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_append(void*,const uint16_t*,uint32_t);
void XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_flushed(void*,int32_t);
void XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_end(void*);
int32_t XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_start(const void*);
uint32_t XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_field(const void*,uint32_t,uint32_t);
void XY_SAMI_FILE_CDECL xy_vsfilter_sami_file_release(void*);
}
#undef XY_SAMI_FILE_CDECL
namespace XyRust {
class SamiFile {
    void* owner;
    SamiFile(const SamiFile&);
    SamiFile& operator=(const SamiFile&);
public:
    SamiFile():owner(xy_vsfilter_sami_file_create()){}
    ~SamiFile(){xy_vsfilter_sami_file_release(owner);}
    template<class String> uint32_t Begin(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        return xy_vsfilter_sami_file_begin(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    template<class String> void Append(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_sami_file_append(owner,units.data(),static_cast<uint32_t>(units.size()));
    }
    void Flushed(int time){xy_vsfilter_sami_file_flushed(owner,time);}
    void End(){xy_vsfilter_sami_file_end(owner);}
    int Start()const{return xy_vsfilter_sami_file_start(owner);}
    template<class String> String Text()const{
        const uint32_t length=xy_vsfilter_sami_file_field(owner,0,0);
        std::vector<wchar_t> units(static_cast<size_t>(length)+1,0);
        for(uint32_t i=0;i<length;++i)units[i]=static_cast<wchar_t>(xy_vsfilter_sami_file_field(owner,1,i));
        return String(units.data(),static_cast<int>(length));
    }
};
}
