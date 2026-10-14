// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef _MSC_VER
#define XY_SSF_LITERAL_CDECL __cdecl
#else
#define XY_SSF_LITERAL_CDECL
#endif
struct XYRustSsfLiteral {uint32_t phase;int32_t quote;int64_t depth;int32_t value;};
static_assert(offsetof(XYRustSsfLiteral,depth)==8,"SSF literal depth layout");
static_assert(offsetof(XYRustSsfLiteral,value)==16,"SSF literal value layout");
extern "C" {
void XY_SSF_LITERAL_CDECL xy_vsfilter_ssf_literal_init(XYRustSsfLiteral*,uint32_t);
uint64_t XY_SSF_LITERAL_CDECL xy_vsfilter_ssf_literal_step(XYRustSsfLiteral*,int32_t);
}
#undef XY_SSF_LITERAL_CDECL
namespace XyRust {
class SsfLiteral {
    XYRustSsfLiteral state;
    uint64_t result;
public:
    enum Action {Skip,SkipBlock,Get,Peek,Append,String,Block,ExpectedQuote,Newline,StringEos,UnterminatedString,ExpectedBrace,BlockEos,UnterminatedBlock};
    explicit SsfLiteral(bool block):result(0){xy_vsfilter_ssf_literal_init(&state,block?1:0);}
    uint32_t Next(int last){result=xy_vsfilter_ssf_literal_step(&state,last);return static_cast<uint32_t>(result);}
    uint16_t Value()const{return static_cast<uint16_t>(result>>32);}
};
}
