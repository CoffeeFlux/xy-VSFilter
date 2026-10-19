// SPDX-License-Identifier: GPL-2.0-or-later
// Included after RustRendererDc.h. HFONT and shared DC ownership are in Rust.
extern "C" {
uint32_t __cdecl xy_vsfilter_font_create_v1(const uint8_t*,size_t,uintptr_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_font_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_font_read_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_font_count_v1(uint8_t*);
}
namespace XyRust {
struct FontIntern::Owner {
    uint64_t handle;
    Owner(const STSStyleBase& s,HDC dc):handle(0){
        const auto name=DocumentDetail::Units(s.fontName);DocumentDetail::Bytes input(24,0);std::array<uint8_t,8> out;
        DocumentDetail::Put(input,0,static_cast<uint32_t>(s.charSet),4);DocumentDetail::Put(input,4,static_cast<uint32_t>(s.fontWeight),4);
        input[8]=s.fItalic;input[9]=s.fUnderline;input[10]=s.fStrikeOut;
        DocumentDetail::Put(input,12,DocumentDetail::Count(name.size()/2),4);uint64_t bits;memcpy(&bits,&s.fontSize,8);DocumentDetail::Put(input,16,bits,8);
        input.insert(input.end(),name.begin(),name.end());
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_font_create_v1(input.data(),input.size(),reinterpret_cast<uintptr_t>(dc),out.data());}));
        handle=DocumentDetail::Load(out,0,8);
    }
    ~Owner(){DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_font_destroy_v1(handle);}));}
    Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
};
FontIntern::FontIntern(const STSStyleBase& s){RendererDcOperation operation;owner_=std::make_shared<Owner>(s,operation.get());}
FontIntern::FontIntern(const STSStyleBase& s,const RendererDcOperation& operation):owner_(std::make_shared<Owner>(s,operation.get())){}
FontSnapshot FontIntern::get()const{
    DocumentDetail::Shape(owner_!=nullptr);std::array<uint8_t,16> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_font_read_v1(owner_->handle,out.data());}));
    FontSnapshot s={reinterpret_cast<HFONT>(static_cast<uintptr_t>(DocumentDetail::Load(out,0,8))),DocumentDetail::I32(DocumentDetail::Load(out,8,4)),DocumentDetail::I32(DocumentDetail::Load(out,12,4))};return s;
}
size_t FontIntern::Count(){std::array<uint8_t,8> out;DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_font_count_v1(out.data());}));return static_cast<size_t>(DocumentDetail::Load(out,0,8));}
}
