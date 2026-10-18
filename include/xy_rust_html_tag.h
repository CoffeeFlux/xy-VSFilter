// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RustDocumentOwner.h"
// Internal stateless synchronous call. Output: accepted,3karaoke i32s,
// style-byte count and the complete existing style wire. No pointers retained.
extern "C" uint32_t __cdecl xy_vsfilter_html_tag_v1(const uint8_t*,size_t,uint8_t*,size_t);
namespace XyRust {
inline bool HtmlTag(const CStringW& text,STSStyle& current,const STSStyle& original,int& kind,int& start,int& end) {
    const auto tag=DocumentDetail::Units(text),a=DocumentDetail::StyleBytes(current),b=DocumentDetail::StyleBytes(original);
    DocumentDetail::Bytes input;
    for(uint32_t value:{DocumentDetail::Count(tag.size()/2),DocumentDetail::Count(a.size()),DocumentDetail::Count(b.size()),static_cast<uint32_t>(kind),static_cast<uint32_t>(start),static_cast<uint32_t>(end)})DocumentDetail::Append(input,value,4);
    for(const auto* part:{&tag,&a,&b})input.insert(input.end(),part->begin(),part->end());
    const size_t fontBytes=(std::max)(tag.size(),(std::max)(a.size()-DocumentDetail::StyleHeaderBytes,b.size()-DocumentDetail::StyleHeaderBytes));
    DocumentDetail::Bytes output(DocumentDetail::AddSize(20+DocumentDetail::StyleHeaderBytes,fontBytes));
    if(xy_vsfilter_html_tag_v1(input.data(),input.size(),output.data(),output.size()))AtlThrow(E_UNEXPECTED);
    const auto accepted=DocumentDetail::Load(output,0,4),length=DocumentDetail::Load(output,16,4);
    DocumentDetail::Shape(accepted<=1 && length>=DocumentDetail::StyleHeaderBytes && length<=output.size()-20);
    DocumentDetail::Bytes style(output.begin()+20,output.begin()+20+static_cast<size_t>(length));
    current=DocumentDetail::StyleValue(style);
    kind=DocumentDetail::I32(DocumentDetail::Load(output,4,4));start=DocumentDetail::I32(DocumentDetail::Load(output,8,4));end=DocumentDetail::I32(DocumentDetail::Load(output,12,4));return accepted!=0;
}
}
