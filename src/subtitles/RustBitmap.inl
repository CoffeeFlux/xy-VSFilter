// SPDX-License-Identifier: GPL-2.0-or-later
// Canonical Rust bitmap owners; native output views contain only initialized bytes.
#include "xy_rust_overlay.h"
#include "xy_rust_composite.h"
extern "C" {
uint32_t __cdecl xy_vsfilter_bitmap_create_v1(const uint8_t*,uint8_t*);
uint32_t __cdecl xy_vsfilter_bitmap_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_bitmap_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_bitmap_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_bitmap_draw_v1(uint64_t,uint64_t,uint64_t,const uint8_t*,const uint8_t*);
uint32_t __cdecl xy_vsfilter_bitmap_action_v1(uint64_t,uint32_t,uint32_t);
}
namespace XyRust {
BitmapOwner::~BitmapOwner(){if(id_)DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_bitmap_destroy_v1(id_);}));}
void BitmapOwner::Create(const int32_t* fields){
    using namespace DocumentDetail;Shape(!id_);std::array<uint8_t,20> bytes;std::array<uint8_t,8> out;
    for(size_t i=0;i<5;++i)Put(bytes,i*4,static_cast<uint32_t>(fields[i]),4);
    Check(Invoke([&](){return xy_vsfilter_bitmap_create_v1(bytes.data(),out.data());}));id_=Load(out,0,8);
}
void BitmapOwner::Draw(const OverlayOwner& overlay,const CompositeOwner* alpha,const int32_t* fields,const uint32_t* switches){
    using namespace DocumentDetail;std::array<uint8_t,28> bytes;
    for(size_t i=0;i<7;++i)Put(bytes,i*4,static_cast<uint32_t>(fields[i]),4);
    Check(Invoke([&](){return xy_vsfilter_bitmap_draw_v1(id_,overlay.id_,alpha?alpha->id_:0,bytes.data(),reinterpret_cast<const uint8_t*>(switches));}));
}
void BitmapOwner::Action(uint32_t action,uint32_t color){DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_bitmap_action_v1(id_,action,color);}));}
std::vector<uint8_t> BitmapOwner::Read()const{
    using namespace DocumentDetail;Loan gate;std::array<uint8_t,8> info;Check(xy_vsfilter_bitmap_info_v1(id_,info.data()));
    Bytes out(Size(Load(info,0,8)));Check(xy_vsfilter_bitmap_read_v1(id_,out.data(),out.size()));return out;
}
}
void XyBitmap::RestoreRust(){
    using namespace XyRust::DocumentDetail;const auto bytes=m_owner.Read();Shape(bytes.size()>=32&&Load(bytes,28,4)==1);
    type=I32(Load(bytes,0,4));x=I32(Load(bytes,4,4));y=I32(Load(bytes,8,4));w=I32(Load(bytes,12,4));h=I32(Load(bytes,16,4));pitch=I32(Load(bytes,20,4));
    Shape((type==PACK||type==PLANNA)&&w>=0&&h>=0&&pitch>=0);
    const size_t plane=MulSize(size_t(pitch),size_t(h)),count=MulSize(plane,type==PLANNA?4:1);Shape(bytes.size()==AddSize(32,MulSize(count,2)));
    if(!bits){const unsigned shift=static_cast<unsigned>(Load(bytes,24,4));
        bits=m_storage.Allocate(count,shift)?m_storage.Data():xy_malloc(count,shift);if(!bits&&count)throw std::bad_alloc();}
    plans[0]=static_cast<BYTE*>(bits);
    if(type==PLANNA)for(size_t i=1;i<4;++i)plans[i]=plans[0]+i*plane;
    for(size_t at=0;at<count;){if(!bytes[32+count+at]){++at;continue;}size_t end=at+1;while(end<count&&bytes[32+count+end])++end;
        memcpy(plans[0]+at,bytes.data()+32+at,end-at);at=end;}
}
void XyBitmap::DrawRust(const XyRust::OverlayOwner& overlay,const XyRust::CompositeOwner* alpha,const CRect& clip,int sx,int sy,const DWORD* switches,bool body,bool border,bool addition){
    const int32_t fields[]={clip.left,clip.top,clip.right,clip.bottom,sx,sy,(body?1:0)|(border?2:0)|(addition?4:0)};
    m_owner.Draw(overlay,alpha,fields,reinterpret_cast<const uint32_t*>(switches));RestoreRust();
}
void XyBitmap::FlipAlphaOwned(){m_owner.Action(0,0);RestoreRust();}
void XyBitmap::DebugBoxOwned(DWORD color){m_owner.Action(1,color);RestoreRust();}
