// SPDX-License-Identifier: GPL-2.0-or-later
// Included once by RTS.cpp after RustDocumentOwner's detached value helpers.
extern "C" {
uint32_t __cdecl xy_vsfilter_string_create_v1(const uint8_t*,size_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_string_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_string_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_string_cache_v1(uint32_t,size_t,uint32_t,uint8_t*);
}
namespace XyRust {
struct StringFlyweight::Owner {
    uint64_t handle;
    size_t legacy_id,units;
    explicit Owner(const CStringW& value):handle(0),legacy_id(0),units(0) {
        const auto input=DocumentDetail::Units(value);std::array<uint8_t,24> out;
        DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_string_create_v1(input.data(),input.size(),out.data());}));
        // Infallible fixed packet decoding after ownership transfer. Length and
        // legacy ID were produced for this architecture from a CString input.
        handle=DocumentDetail::Load(out,0,8);
        legacy_id=static_cast<size_t>(DocumentDetail::Load(out,8,8));
        units=static_cast<size_t>(DocumentDetail::Load(out,16,8));
    }
    ~Owner() {DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_string_destroy_v1(handle);}));}
    Owner(const Owner&)=delete;
    Owner& operator=(const Owner&)=delete;
};
StringFlyweight::StringFlyweight(const CStringW* value) {
    std::unique_ptr<const CStringW> input(value);
    DocumentDetail::Shape(value!=nullptr);owner_=std::make_shared<Owner>(*input);
}
CStringW StringFlyweight::Get()const {
    DocumentDetail::Bytes out(DocumentDetail::MulSize(owner_->units,2));
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_string_read_v1(owner_->handle,out.data(),out.size());}));
    size_t offset=0;return DocumentDetail::String(out,offset,DocumentDetail::Count(owner_->units));
}
StringFlyweight::IdType StringFlyweight::GetId()const {return owner_->legacy_id;}
size_t StringFlyweight::Cacher::Call(uint32_t action,size_t maximum,bool reset,unsigned field) {
    std::array<uint8_t,32> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_string_cache_v1(action,maximum,reset?1:0,out.data());}));
    return static_cast<size_t>(DocumentDetail::Load(out,field*8,8));
}
size_t StringFlyweight::Cacher::GetMaxItemNum()const {return Call(0,0,false,0);}
size_t StringFlyweight::Cacher::GetCurItemNum()const {return Call(0,0,false,1);}
size_t StringFlyweight::Cacher::GetCacheHitCount()const {return Call(0,0,false,2);}
size_t StringFlyweight::Cacher::GetQueryCount()const {return Call(0,0,false,3);}
size_t StringFlyweight::Cacher::SetMaxItemNum(size_t maximum,bool reset) {return Call(1,maximum,reset,0);}
void StringFlyweight::Cacher::RemoveAll(bool reset) {Call(2,0,reset,0);}
void StringFlyweight::Cacher::RemoveTail() {Call(3,0,false,0);}
StringFlyweight::Cacher* StringFlyweight::GetCacher() {static Cacher cache;return &cache;}
}
