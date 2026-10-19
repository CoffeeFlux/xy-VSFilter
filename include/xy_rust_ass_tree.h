// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RustDocumentOwner.h"
#include <memory>
extern "C" {
uint32_t __cdecl xy_vsfilter_ass_tree_create_v1(uint8_t*);
uint32_t __cdecl xy_vsfilter_ass_tree_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_ass_tree_append_v1(uint64_t,uint32_t,const uint8_t*,size_t,void(__cdecl*)(),void(__cdecl*)());
uint32_t __cdecl xy_vsfilter_ass_tree_query_v1(uint64_t,uint32_t,uint32_t,uint32_t,uint8_t*,size_t);
int32_t __cdecl xy_vsfilter_ass_tree_position_v1(uint32_t);
}
namespace XyRust {
class AssTree {
    struct Owner {
        uint64_t id;
        Owner():id(0) {
            std::array<uint8_t,8> bytes;
            DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_ass_tree_create_v1(bytes.data());}));
            id=DocumentDetail::Load(bytes,0,8);
        }
        ~Owner() {DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_ass_tree_destroy_v1(id);}));}
        Owner(const Owner&)=delete;
        Owner& operator=(const Owner&)=delete;
    };
    // Shared native lifetime wrapper only. Every node, raw parameter and child
    // list lives in Rust; list_ is a checked index, never a canonical pointer.
    std::shared_ptr<Owner> owner_;
    uint32_t list_;
    uint64_t Scalar(uint32_t index,uint32_t field)const {
        std::array<uint8_t,8> bytes;
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_ass_tree_query_v1(owner_->id,list_,index,field,bytes.data(),bytes.size());}));
        return DocumentDetail::Load(bytes,0,8);
    }
public:
    AssTree():list_(0){}
    static int PositionLevel(uint32_t command) {return xy_vsfilter_ass_tree_position_v1(command);}
    uint32_t Count()const {return owner_?DocumentDetail::Count(DocumentDetail::Size(Scalar(0,0))):0;}
    void Append(const CStringW& input) {
        if(!owner_)owner_=std::make_shared<Owner>();
        const auto bytes=DocumentDetail::Units(input);
        DocumentDetail::Check(xy_vsfilter_ass_tree_append_v1(owner_->id,list_,bytes.data(),bytes.size(),
            [](){DocumentDetail::SharedGate().Enter();},[](){DocumentDetail::SharedGate().Leave();}));
    }
    void Read(uint32_t index,uint32_t& command,CAtlArray<CStringW>& params,AssTree& children)const {
        DocumentDetail::Shape(owner_!=nullptr);
        DocumentDetail::Bytes bytes(DocumentDetail::Size(Scalar(index,1)));
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_ass_tree_query_v1(owner_->id,list_,index,2,bytes.data(),bytes.size());}));
        DocumentDetail::Shape(bytes.size()>=12);
        const auto cmd=DocumentDetail::Load(bytes,0,4),child=DocumentDetail::Load(bytes,4,4),count=DocumentDetail::Load(bytes,8,4);
        DocumentDetail::Shape(cmd<=54 && child!=0 && count<=(bytes.size()-12)/4);
        size_t offset=12;params.RemoveAll();
        for(size_t i=0;i<count;++i) {
            const size_t units=DocumentDetail::Size(DocumentDetail::Load(bytes,offset,4));offset+=4;
            const size_t length=DocumentDetail::MulSize(units,2);DocumentDetail::Shape(units<=INT32_MAX && length<=bytes.size()-offset);
            CStringW value;WCHAR* buffer=value.GetBufferSetLength(static_cast<int>(units));
            for(size_t u=0;u<units;++u)buffer[u]=static_cast<WCHAR>(DocumentDetail::Load(bytes,offset+u*2,2));
            value.ReleaseBufferSetLength(static_cast<int>(units));params.Add(value);offset+=length;
        }
        DocumentDetail::Shape(offset==bytes.size());command=static_cast<uint32_t>(cmd);
        children.owner_=owner_;children.list_=static_cast<uint32_t>(child);
    }
};
}
