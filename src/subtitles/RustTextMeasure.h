// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RustRendererDc.h"
extern "C" uint32_t __cdecl xy_vsfilter_text_measure_v1(uintptr_t,const uint8_t*,size_t,uint8_t*);
namespace XyRust {
inline void MeasureText(CText::TextInfo& output,const STSStyle& style,const CStringW& text,
    const RendererDcOperation& operation,const FontSnapshot& font) {
    const auto units=DocumentDetail::Units(text);DocumentDetail::Bytes input(48,0);
    const double* values[]={&style.fontScaleX,&style.fontScaleY,&style.fontSpacing};
    for(size_t i=0;i<3;++i){uint64_t bits;memcpy(&bits,values[i],8);DocumentDetail::Put(input,i*8,bits,8);}
    DocumentDetail::Put(input,24,static_cast<uint32_t>(output.m_width),4);
    DocumentDetail::Put(input,28,static_cast<uint32_t>(font.m_ascent),4);
    DocumentDetail::Put(input,32,static_cast<uint32_t>(font.m_descent),4);
    DocumentDetail::Put(input,36,DocumentDetail::Count(units.size()/2),4);
    DocumentDetail::Put(input,40,reinterpret_cast<uintptr_t>(font.handle),8);
    input.insert(input.end(),units.begin(),units.end());std::array<uint8_t,12> out;
    DocumentDetail::Check(xy_vsfilter_text_measure_v1(reinterpret_cast<uintptr_t>(operation.get()),input.data(),input.size(),out.data()));
    output.m_width=DocumentDetail::I32(DocumentDetail::Load(out,0,4));
    output.m_ascent=DocumentDetail::I32(DocumentDetail::Load(out,4,4));
    output.m_descent=DocumentDetail::I32(DocumentDetail::Load(out,8,4));
}
}
