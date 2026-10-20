// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
extern "C" uint32_t __cdecl xy_vsfilter_surface_v1(const uint8_t*,uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*);
namespace XyRust {
inline void Surface(const int32_t* fields,void* destination,const void* source0=nullptr,const void* source1=nullptr,const void* source2=nullptr,const void* source3=nullptr){
    using namespace DocumentDetail;std::array<uint8_t,48> bytes;
    for(size_t i=0;i<12;++i)Put(bytes,i*4,static_cast<uint32_t>(fields[i]),4);
    Check(Invoke([&](){return xy_vsfilter_surface_v1(bytes.data(),static_cast<uint8_t*>(destination),static_cast<const uint8_t*>(source0),static_cast<const uint8_t*>(source1),static_cast<const uint8_t*>(source2),static_cast<const uint8_t*>(source3));}));
}
}
