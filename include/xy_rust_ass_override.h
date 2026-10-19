// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RustDocumentOwner.h"
extern "C" {
uint32_t __cdecl xy_vsfilter_ass_override_v1(uint64_t,uint32_t,uint64_t,uint64_t,const uint8_t*,size_t,void*,uint32_t(__cdecl*)(void*,const uint8_t*,size_t),void(__cdecl*)(),void(__cdecl*)());
double __cdecl xy_vsfilter_ass_animation_v1(double,double,uint32_t,int32_t,int32_t,int32_t,int32_t,double);
}
namespace XyRust {
// Transitional native mirrors and clip construction only; all dispatch,
// animation and mutation decisions belong to the safe Rust evaluator.
class AssOverride {
public:
    static uint32_t Apply(const AssTree& tree,const DocumentOwner& document,Effects& effects,
        const DocumentDetail::Bytes& input,void* context,uint32_t(__cdecl* callback)(void*,const uint8_t*,size_t)) {
        return xy_vsfilter_ass_override_v1(tree.owner_?tree.owner_->id:0,tree.list_,document.id_,effects.id_,
            input.data(),input.size(),context,callback,[](){DocumentDetail::SharedGate().Enter();},[](){DocumentDetail::SharedGate().Leave();});
    }
};
}
