// SPDX-License-Identifier: GPL-2.0-or-later
// Included once by STS.cpp after detached DocumentDetail style helpers.
extern "C" {
uint32_t __cdecl xy_vsfilter_style_create_v1(const uint8_t*,size_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_style_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_style_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_style_count_v1(uint8_t*);
}
namespace XyRust {
struct StyleIntern::Owner {
    uint64_t handle,identity;
    size_t length;
    explicit Owner(const STSStyle& value):handle(0),identity(0),length(0) {
        const auto input=DocumentDetail::StyleBytes(value);std::array<uint8_t,24> out;
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_style_create_v1(input.data(),input.size(),out.data());}));
        handle=DocumentDetail::Load(out,0,8);identity=DocumentDetail::Load(out,8,8);
        length=static_cast<size_t>(DocumentDetail::Load(out,16,8));
    }
    ~Owner() {DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_style_destroy_v1(handle);}));}
    Owner(const Owner&)=delete;
    Owner& operator=(const Owner&)=delete;
};
StyleIntern::StyleIntern():owner_(std::make_shared<Owner>(STSStyle())){}
StyleIntern::StyleIntern(const STSStyle& value):owner_(std::make_shared<Owner>(value)){}
StyleIntern& StyleIntern::operator=(const STSStyle& value) {owner_=std::make_shared<Owner>(value);return *this;}
uint64_t StyleIntern::Identity()const {return owner_?owner_->identity:0;}
STSStyle StyleIntern::get()const {
    DocumentDetail::Shape(owner_!=nullptr);DocumentDetail::Bytes out(owner_->length);
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_style_read_v1(owner_->handle,out.data(),out.size());}));
    return DocumentDetail::StyleValue(out);
}
StyleIntern::operator STSStyle()const {return get();}
size_t StyleIntern::Count() {
    std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_style_count_v1(out.data());}));
    return static_cast<size_t>(DocumentDetail::Load(out,0,8));
}
}
