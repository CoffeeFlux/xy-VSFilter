// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <list>
#include <map>
#include <vector>
#ifdef _MSC_VER
#define XY_REALTEXT_CDECL __cdecl
#else
#define XY_REALTEXT_CDECL
#endif
extern "C" {
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_document_create();
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_document_next(void*, uint32_t, uint16_t, uint32_t, const uint16_t*, size_t, uint32_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_document_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_lists_create();
void XY_REALTEXT_CDECL xy_vsfilter_realtext_lists_begin(void*, const uint16_t*, size_t, uint32_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_lists_attribute(void*, const uint16_t*, size_t, const uint16_t*, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_lists_action(void*, uint32_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_lists_field(const void*, uint32_t, size_t, uint32_t, size_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_lists_release(void*);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_format(int32_t, int32_t, int32_t*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_timeline_create();
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_timeline_select(void*, int32_t, uint32_t, uint32_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_timeline_advance(void*, int32_t, int32_t);
int32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_timeline_field(const void*, uint32_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_timeline_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_lower_create();
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_lower_next(void*, const uint16_t*, size_t, uint16_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_lower_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_lower_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_text_create();
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_text_next(void*, const uint16_t*, size_t, uint32_t, uint32_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_text_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_text_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_tag_create();
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_tag_next(void*, const uint16_t*, size_t, uint32_t, uint32_t, uint32_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_tag_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_tag_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_create(uint32_t, int32_t, int32_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_begin(void*, const uint16_t*, size_t, uint32_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_attribute_kind(const void*, const uint16_t*, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_attribute(void*, const uint16_t*, size_t, const uint16_t*, size_t, uint32_t, int32_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_end(void*);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_create(uint32_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_next(void*, const uint16_t*, size_t, uint32_t, uint32_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_attributes_release(void*);
void* XY_REALTEXT_CDECL xy_vsfilter_realtext_timecode_create(size_t);
uint32_t XY_REALTEXT_CDECL xy_vsfilter_realtext_timecode_next(void*, const uint16_t*, size_t);
size_t XY_REALTEXT_CDECL xy_vsfilter_realtext_timecode_field(const void*, uint32_t, size_t);
void XY_REALTEXT_CDECL xy_vsfilter_realtext_timecode_release(void*);
}
#undef XY_REALTEXT_CDECL
namespace XyRust {
class RealTextDocument {
    void* owner;
    RealTextDocument(const RealTextDocument&);
    RealTextDocument& operator=(const RealTextDocument&);
public:
    RealTextDocument():owner(xy_vsfilter_realtext_document_create()){}
    ~RealTextDocument(){xy_vsfilter_realtext_document_release(owner);}
    template<class Tag> uint32_t Next(const std::wstring& input,bool response,const Tag& tag){
        const bool empty=input.empty();const uint16_t first=empty?0:static_cast<uint16_t>(input.at(0));
        const uint32_t flags=(tag.m_bOpen?1u:0u)|(tag.m_bClose?2u:0u)|(tag.m_bComment?4u:0u)|(tag.m_bText?8u:0u);
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_document_next(owner,empty?1:0,first,response?1:0,reinterpret_cast<const uint16_t*>(tag.m_szName.data()),tag.m_szName.size(),flags);
#else
        const std::vector<uint16_t> name(tag.m_szName.begin(),tag.m_szName.end());
        return xy_vsfilter_realtext_document_next(owner,empty?1:0,first,response?1:0,name.data(),name.size(),flags);
#endif
    }
};
class RealTextLists {
    void* owner;
    RealTextLists(const RealTextLists&);
    RealTextLists& operator=(const RealTextLists&);
    size_t Field(uint32_t group,size_t index,uint32_t field,size_t attribute=0,size_t unit=0)const{return xy_vsfilter_realtext_lists_field(owner,group,index,field,attribute,unit);}
    std::wstring String(uint32_t group,size_t index,uint32_t field,size_t attribute=0)const{
        const size_t length=Field(group,index,field,attribute);std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(Field(group,index,field+1,attribute,i));
        return result;
    }
public:
    RealTextLists():owner(xy_vsfilter_realtext_lists_create()){}
    ~RealTextLists(){xy_vsfilter_realtext_lists_release(owner);}
    template<class Tag> void Set(const Tag& tag){
        const std::vector<uint16_t> name(tag.m_szName.begin(),tag.m_szName.end());
        const uint32_t flags=(tag.m_bOpen?1u:0u)|(tag.m_bClose?2u:0u)|(tag.m_bComment?4u:0u)|(tag.m_bText?8u:0u);
        xy_vsfilter_realtext_lists_begin(owner,name.data(),name.size(),flags);
        for(const auto& item:tag.m_mapAttributes){
            const std::vector<uint16_t> key(item.first.begin(),item.first.end()),value(item.second.begin(),item.second.end());
            xy_vsfilter_realtext_lists_attribute(owner,key.data(),key.size(),value.data(),value.size());
        }
    }
    void Open(){xy_vsfilter_realtext_lists_action(owner,1);}
    void Close(){xy_vsfilter_realtext_lists_action(owner,2);}
    void Append(){xy_vsfilter_realtext_lists_action(owner,0);}
    template<class Tag> void Append(const Tag& tag){Set(tag);Append();}
    void Clear(){xy_vsfilter_realtext_lists_action(owner,3);}
    template<class Tag> std::list<Tag> Export(uint32_t group=0)const{
        std::list<Tag> result;const size_t count=Field(group,0,0);
        for(size_t i=0;i<count;++i){
            Tag tag;tag.m_szName=String(group,i,2);const uint32_t flags=static_cast<uint32_t>(Field(group,i,1));
            tag.m_bOpen=(flags&1)!=0;tag.m_bClose=(flags&2)!=0;tag.m_bComment=(flags&4)!=0;tag.m_bText=(flags&8)!=0;
            const size_t attributes=Field(group,i,4);
            for(size_t a=0;a<attributes;++a)tag.m_mapAttributes[String(group,i,5,a)]=String(group,i,7,a);
            result.push_back(tag);
        }
        return result;
    }
};
class RealTextTimeline {
    void* owner;
    RealTextTimeline(const RealTextTimeline&);
    RealTextTimeline& operator=(const RealTextTimeline&);
public:
    RealTextTimeline():owner(xy_vsfilter_realtext_timeline_create()){}
    ~RealTextTimeline(){xy_vsfilter_realtext_timeline_release(owner);}
    uint32_t Select(int next,bool finalLine,bool mapEmpty){return xy_vsfilter_realtext_timeline_select(owner,next,finalLine?1:0,mapEmpty?1:0);}
    int First()const{return xy_vsfilter_realtext_timeline_field(owner,0);}
    int Second()const{return xy_vsfilter_realtext_timeline_field(owner,1);}
    void Advance(int start,int end){xy_vsfilter_realtext_timeline_advance(owner,start,end);}
};
class RealTextLower {
    void* owner;
    RealTextLower(const RealTextLower&);
    RealTextLower& operator=(const RealTextLower&);
public:
    RealTextLower():owner(xy_vsfilter_realtext_lower_create()){}
    ~RealTextLower(){xy_vsfilter_realtext_lower_release(owner);}
    bool Next(const std::wstring& input,wchar_t response){
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_lower_next(owner,reinterpret_cast<const uint16_t*>(input.data()),input.size(),static_cast<uint16_t>(response))!=0;
#else
        const std::vector<uint16_t> units(input.begin(),input.end());
        return xy_vsfilter_realtext_lower_next(owner,units.data(),units.size(),static_cast<uint16_t>(response))!=0;
#endif
    }
    wchar_t Character()const{return static_cast<wchar_t>(xy_vsfilter_realtext_lower_field(owner,0,0));}
    std::wstring Value()const{
        const size_t length=xy_vsfilter_realtext_lower_field(owner,1,0);std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_lower_field(owner,2,i));
        return result;
    }
};
class RealTextText {
    void* owner;
    RealTextText(const RealTextText&);
    RealTextText& operator=(const RealTextText&);
public:
    RealTextText():owner(xy_vsfilter_realtext_text_create()){}
    ~RealTextText(){xy_vsfilter_realtext_text_release(owner);}
    uint32_t Next(const std::wstring& input,bool ignore,bool response){
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_text_next(owner,reinterpret_cast<const uint16_t*>(input.data()),input.size(),ignore?1:0,response?1:0);
#else
        const std::vector<uint16_t> units(input.begin(),input.end());
        return xy_vsfilter_realtext_text_next(owner,units.data(),units.size(),ignore?1:0,response?1:0);
#endif
    }
    size_t Field(uint32_t field)const{return xy_vsfilter_realtext_text_field(owner,field,0);}
    std::wstring Value()const{
        const size_t length=Field(2);std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_text_field(owner,3,i));
        return result;
    }
};
class RealTextTag {
    void* owner;
    RealTextTag(const RealTextTag&);
    RealTextTag& operator=(const RealTextTag&);
public:
    RealTextTag():owner(xy_vsfilter_realtext_tag_create()){}
    ~RealTextTag(){xy_vsfilter_realtext_tag_release(owner);}
    uint32_t Next(const std::wstring& input,bool ignore,bool response,uint32_t position){
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_tag_next(owner,reinterpret_cast<const uint16_t*>(input.data()),input.size(),ignore?1:0,response?1:0,position);
#else
        const std::vector<uint16_t> units(input.begin(),input.end());
        return xy_vsfilter_realtext_tag_next(owner,units.data(),units.size(),ignore?1:0,response?1:0,position);
#endif
    }
    size_t Field(uint32_t field)const{return xy_vsfilter_realtext_tag_field(owner,field,0);}
    std::wstring Text()const{
        const size_t length=Field(4);std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_tag_field(owner,5,i));
        return result;
    }
};
class RealTextTimecode {
    void* owner;
    RealTextTimecode(const RealTextTimecode&);
    RealTextTimecode& operator=(const RealTextTimecode&);
public:
    explicit RealTextTimecode(size_t length):owner(xy_vsfilter_realtext_timecode_create(length)){}
    ~RealTextTimecode(){xy_vsfilter_realtext_timecode_release(owner);}
    uint32_t Next(const std::wstring& input){
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_timecode_next(owner,reinterpret_cast<const uint16_t*>(input.data()),input.size());
#else
        const std::vector<uint16_t> units(input.begin(),input.end());
        return xy_vsfilter_realtext_timecode_next(owner,units.data(),units.size());
#endif
    }
    bool Last()const{return xy_vsfilter_realtext_timecode_field(owner,0,0)!=0;}
    size_t Missing()const{return xy_vsfilter_realtext_timecode_field(owner,1,0);}
    std::wstring Part()const{
        const size_t length=xy_vsfilter_realtext_timecode_field(owner,2,0);
        std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_timecode_field(owner,3,i));
        return result;
    }
};
class RealTextAttributes {
    void* owner;
    RealTextAttributes(const RealTextAttributes&);
    RealTextAttributes& operator=(const RealTextAttributes&);
    std::wstring String(uint32_t field)const{
        const size_t length=xy_vsfilter_realtext_attributes_field(owner,field,0);
        std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_attributes_field(owner,field+1,i));
        return result;
    }
public:
    explicit RealTextAttributes(uint32_t position):owner(xy_vsfilter_realtext_attributes_create(position)){}
    ~RealTextAttributes(){xy_vsfilter_realtext_attributes_release(owner);}
    uint32_t Next(const std::wstring& input,bool ignore,bool response){
#ifdef _WIN32
        static_assert(sizeof(wchar_t)==sizeof(uint16_t),"native strings must contain UTF-16 units");
        return xy_vsfilter_realtext_attributes_next(owner,reinterpret_cast<const uint16_t*>(input.data()),input.size(),ignore?1:0,response?1:0);
#else
        const std::vector<uint16_t> units(input.begin(),input.end());
        return xy_vsfilter_realtext_attributes_next(owner,units.data(),units.size(),ignore?1:0,response?1:0);
#endif
    }
    uint32_t Position()const{return static_cast<uint32_t>(xy_vsfilter_realtext_attributes_field(owner,0,0));}
    wchar_t Character()const{return static_cast<wchar_t>(xy_vsfilter_realtext_attributes_field(owner,1,0));}
    std::wstring Name()const{return String(2);}
    std::wstring Value()const{return String(4);}
};
class RealTextTags {
    void* owner;
    RealTextTags(const RealTextTags&);
    RealTextTags& operator=(const RealTextTags&);
public:
    RealTextTags(uint32_t ignore,int minimum,int maximum):owner(xy_vsfilter_realtext_create(ignore,minimum,maximum)){}
    ~RealTextTags(){xy_vsfilter_realtext_release(owner);}
    bool Begin(const std::wstring& name,uint32_t flags){
        const std::vector<uint16_t> units(name.begin(),name.end());
        return xy_vsfilter_realtext_begin(owner,units.data(),units.size(),flags)!=0;
    }
    uint32_t AttributeKind(const std::wstring& name)const{
        const std::vector<uint16_t> units(name.begin(),name.end());
        return xy_vsfilter_realtext_attribute_kind(owner,units.data(),units.size());
    }
    void Attribute(const std::wstring& name,const std::wstring& value,bool numeric,int size){
        const std::vector<uint16_t> key(name.begin(),name.end()),text(value.begin(),value.end());
        xy_vsfilter_realtext_attribute(owner,key.data(),key.size(),text.data(),text.size(),numeric?1:0,size);
    }
    void End(){xy_vsfilter_realtext_end(owner);}
    std::wstring Result()const{
        const size_t length=xy_vsfilter_realtext_field(owner,0,0);
        std::wstring result(length,L'\0');
        for(size_t i=0;i<length;++i)result[i]=static_cast<wchar_t>(xy_vsfilter_realtext_field(owner,1,i));
        return result;
    }
};
}
