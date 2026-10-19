// SPDX-License-Identifier: GPL-2.0-or-later
// Included once by STS.cpp after the detached DocumentDetail value helpers.
extern "C" {
uint32_t __cdecl xy_vsfilter_rect_create_v1(const uint8_t*,uint8_t*);
uint32_t __cdecl xy_vsfilter_rect_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_rect_read_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_rect_count_v1(uint8_t*);
}
namespace XyRust {
struct RectIntern::Owner {
    uint64_t handle,identity;
    explicit Owner(const CRect& value):handle(0),identity(0) {
        std::array<uint8_t,16> in,out;
        const LONG coordinates[]={value.left,value.top,value.right,value.bottom};
        for(unsigned i=0;i<4;++i)DocumentDetail::Put(in,i*4,static_cast<uint32_t>(coordinates[i]),4);
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_rect_create_v1(in.data(),out.data());}));
        handle=DocumentDetail::Load(out,0,8);identity=DocumentDetail::Load(out,8,8);
    }
    ~Owner() {DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_rect_destroy_v1(handle);}));}
    Owner(const Owner&)=delete;
    Owner& operator=(const Owner&)=delete;
};
RectIntern::RectIntern():owner_(std::make_shared<Owner>(CRect())){}
RectIntern::RectIntern(const CRect& value):owner_(std::make_shared<Owner>(value)){}
RectIntern& RectIntern::operator=(const CRect& value) {
    // Intern before releasing the old owner, including self-equivalent values.
    owner_=std::make_shared<Owner>(value);return *this;
}
uint64_t RectIntern::Identity()const {return owner_?owner_->identity:0;}
CRect RectIntern::get()const {
    DocumentDetail::Shape(owner_!=nullptr);
    std::array<uint8_t,16> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_rect_read_v1(owner_->handle,out.data());}));
    return CRect(DocumentDetail::I32(DocumentDetail::Load(out,0,4)),DocumentDetail::I32(DocumentDetail::Load(out,4,4)),
        DocumentDetail::I32(DocumentDetail::Load(out,8,4)),DocumentDetail::I32(DocumentDetail::Load(out,12,4)));
}
size_t RectIntern::Count() {
    std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_rect_count_v1(out.data());}));
    return static_cast<size_t>(DocumentDetail::Load(out,0,8));
}
}
