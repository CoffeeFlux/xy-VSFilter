// SPDX-License-Identifier: GPL-2.0-or-later
// Canonical alpha-mask owners survive native Draw/AdditionDraw and view copies.
extern "C" {
uint32_t __cdecl xy_vsfilter_composite_mask_create_v1(uint64_t,uint64_t,const uint8_t*,const uint8_t*,uint8_t*);
uint32_t __cdecl xy_vsfilter_composite_mask_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_composite_mask_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_composite_mask_read_v1(uint64_t,uint8_t*,size_t);
}
namespace XyRust {
CompositeOwner::~CompositeOwner(){if(id_)DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_composite_mask_destroy_v1(id_);}));}
std::shared_ptr<CompositeOwner> CompositeOwner::Build(const OverlayOwner& overlay,const ClipMaskOwner* mask,const int32_t* fields,const uint32_t* switches){
    using namespace DocumentDetail;std::array<uint8_t,28> parameters;std::array<uint8_t,8> out;
    for(size_t i=0;i<7;++i)Put(parameters,i*4,static_cast<uint32_t>(fields[i]),4);
    Check(Invoke([&](){return xy_vsfilter_composite_mask_create_v1(overlay.id_,mask?mask->id_:0,parameters.data(),reinterpret_cast<const uint8_t*>(switches),out.data());}));
    CompositeOwner guard;guard.id_=Load(out,0,8);if(!guard.id_)return {};
    std::shared_ptr<CompositeOwner> result(DEBUG_NEW CompositeOwner());result->id_=guard.id_;guard.id_=0;return result;
}
std::vector<uint8_t> CompositeOwner::Read()const{
    using namespace DocumentDetail;Loan gate;std::array<uint8_t,8> info;Check(xy_vsfilter_composite_mask_info_v1(id_,info.data()));
    Bytes out(Size(Load(info,0,8)));Check(xy_vsfilter_composite_mask_read_v1(id_,out.data(),out.size()));return out;
}
}
