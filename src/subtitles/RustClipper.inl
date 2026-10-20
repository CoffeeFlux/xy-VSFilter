// SPDX-License-Identifier: GPL-2.0-or-later
// Scalar request packets and detached native mask views; canonical pixels are Rust owned.
#include <array>
extern "C" {
uint32_t __cdecl xy_vsfilter_clip_begin_v1(const uint8_t*,uint8_t*);
uint32_t __cdecl xy_vsfilter_clip_request_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_clip_finish_v1(uint64_t,uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_clip_mask_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_clip_mask_clone_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_clip_mask_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_clip_mask_read_v1(uint64_t,uint8_t*,size_t);
}
namespace XyRust {
ClipRequest::ClipRequest(int w,int h,bool inverse,int effect,const int* p,const double& sx,const double& sy){
    using namespace DocumentDetail;std::array<uint8_t,48> bytes={};std::array<uint8_t,8> out;
    const int fields[]={w,h,inverse?1:0,effect,p[0],p[1],p[2],p[4]};
    for(size_t i=0;i<8;++i)Put(bytes,i*4,static_cast<uint32_t>(fields[i]),4);
    uint64_t x,y;memcpy(&x,&sx,8);memcpy(&y,&sy,8);Put(bytes,32,x,8);Put(bytes,40,y,8);
    Check(Invoke([&](){return xy_vsfilter_clip_begin_v1(bytes.data(),out.data());}));id_=Load(out,0,8);
}
ClipRequest::~ClipRequest(){if(id_)DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_clip_request_destroy_v1(id_);}));}
ClipMaskOwner::~ClipMaskOwner(){Take(0);}
void ClipMaskOwner::Take(uint64_t id){if(id_)DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_clip_mask_destroy_v1(id_);}));id_=id;}
ClipMaskOwner::ClipMaskOwner(const ClipMaskOwner& other){*this=other;}
ClipMaskOwner& ClipMaskOwner::operator=(const ClipMaskOwner& other){if(this!=&other){std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_clip_mask_clone_v1(other.id_,out.data());}));Take(DocumentDetail::Load(out,0,8));}return *this;}
std::vector<uint8_t> ClipMaskOwner::Read()const{
    using namespace DocumentDetail;Loan gate;std::array<uint8_t,8> info;Check(xy_vsfilter_clip_mask_info_v1(id_,info.data()));
    Bytes out(Size(Load(info,0,8)));Check(xy_vsfilter_clip_mask_read_v1(id_,out.data(),out.size()));return out;
}
uint64_t OverlayOwner::Clip(uint64_t request)const{std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_clip_finish_v1(request,id_,out.data());}));return DocumentDetail::Load(out,0,8);}
}
bool GrayImage2::RestoreRust(const Overlay& overlay){
    using namespace XyRust::DocumentDetail;const auto bytes=m_owner.Read();Shape(bytes.size()>=24);
    left_top.SetPoint(I32(Load(bytes,0,4)),I32(Load(bytes,4,4)));size.SetSize(I32(Load(bytes,8,4)),I32(Load(bytes,12,4)));pitch=I32(Load(bytes,16,4));
    const unsigned flags=static_cast<unsigned>(Load(bytes,20,4));Shape((flags&~3)==0&&size.cx>=0&&size.cy>=0&&pitch>=size.cx);
    const size_t count=(flags&1)?MulSize(size_t(pitch),size_t(size.cy)):0;Shape(bytes.size()==AddSize(24,MulSize(count,2)));
    if(flags&2){Shape(bool(overlay.mBody)==bool(flags&1));data=overlay.mBody;return true;}
    Shape((flags&1)!=0);XyRust::PlaneAllocation allocation(count);if(!allocation.Ready())return false;
    for(size_t at=0;at<count;){if(!bytes[24+count+at]){++at;continue;}size_t end=at+1;while(end<count&&bytes[24+count+end])++end;
        memcpy(allocation.Data()+at,bytes.data()+24+at,end-at);at=end;}
    allocation.Attach(data);return true;
}
