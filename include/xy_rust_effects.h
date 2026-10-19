// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Included after the transitional native Effect value declaration in RTS.h.
#include "RustDocumentOwner.h"
extern "C" {
uint32_t __cdecl xy_vsfilter_effects_create_v1(uint8_t*);
uint32_t __cdecl xy_vsfilter_effects_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_effects_clear_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_effects_get_v1(uint64_t,uint32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_effects_apply_v1(uint64_t,const uint8_t*,size_t,uint8_t*,size_t,void(__cdecl*)(),void(__cdecl*)());
}
namespace XyRust {
class Effects {
    friend class AssOverride;
    uint64_t id_;
    Effects(const Effects&)=delete;
    Effects& operator=(const Effects&)=delete;
public:
    // A detached value, never a view or pointer into Rust-owned state. Native
    // readers keep their field syntax while ownership and mutation live in Rust.
    class Value {
        bool present_;
        Effect effect_;
    public:
        explicit Value(const std::array<uint8_t,56>& bytes):present_(DocumentDetail::Load(bytes,0,4)!=0) {
            effect_.type=static_cast<eftype>(DocumentDetail::Load(bytes,4,4));
            for(size_t i=0;i<8;++i)effect_.param[i]=DocumentDetail::I32(DocumentDetail::Load(bytes,8+i*4,4));
            for(size_t i=0;i<4;++i)effect_.t[i]=DocumentDetail::I32(DocumentDetail::Load(bytes,40+i*4,4));
        }
        explicit operator bool()const {return present_;}
        const Effect* operator->()const {return &effect_;}
        const Effect& operator*()const {return effect_;}
    };
    Effects():id_(0) {
        std::array<uint8_t,8> bytes;
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_effects_create_v1(bytes.data());}));
        id_=DocumentDetail::Load(bytes,0,8);
    }
    ~Effects() {DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_effects_destroy_v1(id_);}));}
    void Clear() {DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_effects_clear_v1(id_);}));}
    Value operator[](int slot)const {
        std::array<uint8_t,56> bytes;
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_effects_get_v1(id_,DocumentDetail::Index(slot),bytes.data());}));
        return Value(bytes);
    }
    void Apply(unsigned command,const CAtlArray<CStringW>& params,double sx,double sy,int& wrap,int& hard,bool& animated) {
        DocumentDetail::Bytes input;
        DocumentDetail::Append(input,command,4);
        uint64_t bx,by;memcpy(&bx,&sx,8);memcpy(&by,&sy,8);
        DocumentDetail::Append(input,bx,8);DocumentDetail::Append(input,by,8);
        for(uint32_t value:{static_cast<uint32_t>(wrap),static_cast<uint32_t>(hard),static_cast<uint32_t>(animated),DocumentDetail::Count(params.GetCount())})DocumentDetail::Append(input,value,4);
        for(size_t i=0;i<params.GetCount();++i){const auto units=DocumentDetail::Units(params[i]);DocumentDetail::Append(input,DocumentDetail::Count(units.size()/2),4);input.insert(input.end(),units.begin(),units.end());}
        std::array<uint8_t,12> output;
        DocumentDetail::Check(xy_vsfilter_effects_apply_v1(id_,input.data(),input.size(),output.data(),output.size(),
            [](){DocumentDetail::SharedGate().Enter();},[](){DocumentDetail::SharedGate().Leave();}));
        wrap=DocumentDetail::I32(DocumentDetail::Load(output,0,4));hard=DocumentDetail::I32(DocumentDetail::Load(output,4,4));animated=DocumentDetail::Load(output,8,4)!=0;
    }
    void Column(const CStringW& input,double sx,double sy,int& wrap,int& hard,bool& animated) {
        CAtlArray<CStringW> params;params.Add(input);Apply(0,params,sx,sy,wrap,hard,animated);
    }
};
}
