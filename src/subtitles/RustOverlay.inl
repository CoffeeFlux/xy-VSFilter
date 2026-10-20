// SPDX-License-Identifier: GPL-2.0-or-later
// Included once after Rasterizer.h; private owner IDs and detached wire packets.
extern "C" {
uint32_t __cdecl xy_vsfilter_overlay_be_blur_v1(uint64_t,uint32_t,uint32_t,uint32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_overlay_import_v1(const uint8_t*,size_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_overlay_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_overlay_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_overlay_variance_v1(uint64_t,uint32_t,uint32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_overlay_clone_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_overlay_offset_v1(uint64_t,int32_t,int32_t,uint8_t*);
}
namespace XyRust {
uint64_t OverlayOwner::BeBlur(float be,float sx,float sy)const{std::array<uint8_t,8> out;
    uint32_t b,x,y;memcpy(&b,&be,4);memcpy(&x,&sx,4);memcpy(&y,&sy,4);
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_overlay_be_blur_v1(id_,b,x,y,out.data());}));return DocumentDetail::Load(out,0,8);}
OverlayOwner::OverlayOwner(const OverlayOwner& other){*this=other;}
OverlayOwner& OverlayOwner::operator=(const OverlayOwner& other){if(this!=&other){std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_overlay_clone_v1(other.id_,out.data());}));Take(DocumentDetail::Load(out,0,8));}return *this;}
void OverlayOwner::Offset(int x,int y){std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_overlay_offset_v1(id_,x,y,out.data());}));id_=DocumentDetail::Load(out,0,8);}
void OverlayOwner::Clear(){if(id_){DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_raster_destroy_v1(id_);}));id_=0;}}
void OverlayOwner::Take(uint64_t id){Clear();id_=id;}
void OverlayOwner::Import(const std::vector<uint8_t>& bytes){std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_overlay_import_v1(bytes.data(),bytes.size(),out.data());}));Take(DocumentDetail::Load(out,0,8));}
std::vector<uint8_t> OverlayOwner::Read()const{
    if(!id_)return std::vector<uint8_t>(32,0);
    DocumentDetail::Loan gate;std::array<uint8_t,8> info;DocumentDetail::Check(xy_vsfilter_overlay_info_v1(id_,info.data()));
    DocumentDetail::Bytes bytes(DocumentDetail::Size(DocumentDetail::Load(info,0,8)));DocumentDetail::Check(xy_vsfilter_overlay_read_v1(id_,bytes.data(),bytes.size()));return bytes;
}
uint64_t OverlayOwner::Variance(unsigned x,unsigned y)const{std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_overlay_variance_v1(id_,x,y,out.data());}));return DocumentDetail::Load(out,0,8);}
}

void Overlay::ImportNative(bool gaussian,int padding_rows){
    using namespace XyRust::DocumentDetail;
    Shape(mOverlayWidth>=0&&mOverlayHeight>=0&&mOverlayPitch>=mOverlayWidth);
    const size_t count=MulSize(static_cast<size_t>(mOverlayPitch),static_cast<size_t>(mOverlayHeight));
    const unsigned flags=(mfWideOutlineEmpty?1:0)|(mBody?2:0)|(mBorder?4:0);
    const size_t payload=MulSize(count,size_t(bool(mBody))+size_t(bool(mBorder)));
    Bytes bytes(AddSize(32,MulSize(payload,2)),0);
    const int fields[]={mOffsetX,mOffsetY,mWidth,mHeight,mOverlayWidth,mOverlayHeight,mOverlayPitch,static_cast<int>(flags)};
    for(size_t i=0;i<8;++i)Put(bytes,4*i,static_cast<uint32_t>(fields[i]),4);
    Shape(!gaussian||(padding_rows>=0&&padding_rows<=mOverlayHeight/2));
    size_t at=0;for(unsigned plane=0;plane<2;++plane){const auto& source=plane?mBorder:mBody;if(!source)continue;
        for(int row=0;row<mOverlayHeight;++row){
            const bool full=!gaussian||(plane==0&&!mfWideOutlineEmpty&&(row<padding_rows||row>=mOverlayHeight-padding_rows));
            const size_t n=static_cast<size_t>(full?mOverlayPitch:mOverlayWidth),offset=at+size_t(row)*mOverlayPitch;
            memcpy(bytes.data()+32+offset,source.get()+size_t(row)*mOverlayPitch,n);
            memset(bytes.data()+32+payload+offset,1,n);
        }at+=count;
    }
    m_owner.Import(bytes);
}
bool Overlay::RestoreRust(){
    using namespace XyRust::DocumentDetail;const auto bytes=m_owner.Read();Shape(bytes.size()>=32);
    mOffsetX=I32(Load(bytes,0,4));mOffsetY=I32(Load(bytes,4,4));mWidth=I32(Load(bytes,8,4));mHeight=I32(Load(bytes,12,4));
    mOverlayWidth=I32(Load(bytes,16,4));mOverlayHeight=I32(Load(bytes,20,4));mOverlayPitch=I32(Load(bytes,24,4));
    const unsigned flags=static_cast<unsigned>(Load(bytes,28,4));mfWideOutlineEmpty=(flags&1)!=0;
    if(!(flags&6)){Shape(bytes.size()==32);return true;}
    Shape(mOverlayPitch>0&&mOverlayHeight>0);const size_t count=MulSize(size_t(mOverlayPitch),size_t(mOverlayHeight));
    const size_t payload=MulSize(count,size_t(bool(flags&2))+size_t(bool(flags&4)));Shape(bytes.size()==AddSize(32,MulSize(payload,2)));
    size_t at=0;for(unsigned plane=0;plane<2;++plane){if(!(flags&(2<<plane)))continue;
        XyRust::PlaneAllocation allocation(count);if(!allocation.Ready())return false;
        const auto* mask=bytes.data()+32+payload+at;const auto* data=bytes.data()+32+at;
        // Materialize only initialized runs. Unknown padding remains unwritten.
        for(size_t start=0;start<count;){if(!mask[start]){++start;continue;}size_t end=start+1;while(end<count&&mask[end])++end;
            memcpy(allocation.Data()+start,data+start,end-start);start=end;}
        allocation.Attach(plane?mBorder:mBody);at+=count;
    }return true;
}
