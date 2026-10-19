// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup style interner observations. NaN keys stay alive: the native
// deleter may leave an expired raw key, so arbitrary post-release lookup is NOT
// included as passing evidence. A separate child observes count after one drop.
#include <stdafx.h>
#include <STS.h>
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include <RustDocumentOwner.h>
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>
#include <float.h>
#include <process.h>
#include <cfenv>
#include <utility>

static uint64_t domains() {
    unsigned x87=0,sse=0;
#ifdef _M_IX86
    _statusfp2(&x87,&sse);
#else
    sse=_statusfp();
#endif
    return (uint64_t(x87)<<32)|sse;
}
static double STSStyle::* fields[]={&STSStyle::fontSize,&STSStyle::outlineWidthX,&STSStyle::outlineWidthY,
    &STSStyle::shadowDepthX,&STSStyle::shadowDepthY,&STSStyle::fontScaleX,&STSStyle::fontScaleY,
    &STSStyle::fontSpacing,&STSStyle::fBlur,&STSStyle::fGaussianBlur,&STSStyle::fontAngleZ,
    &STSStyle::fontAngleX,&STSStyle::fontAngleY,&STSStyle::fontShiftX,&STSStyle::fontShiftY};
static uint64_t bits(const double& value) {uint64_t result;memcpy(&result,&value,8);return result;}
static bool nan(const STSStyle& value) {
    for(auto field:fields) {const uint64_t n=bits(value.*field);if((n&0x7ff0000000000000ull)==0x7ff0000000000000ull && (n&0xfffffffffffffull))return true;}
    return false;
}
static std::vector<std::shared_ptr<FwSTSStyle>> finite_owners,nan_owners;
static struct LateStyle {
    std::shared_ptr<FwSTSStyle> value;
    ~LateStyle() {
        if(!value)return;
        bool valid;{const STSStyle& s=value->get();valid=s.fontName==L"late-style" && s.fontSize==18;}
        value.reset();
        printf("style-late count=%llu rects=%llu valid=%u\n",static_cast<unsigned long long>(FwSTSStyle::Count()),static_cast<unsigned long long>(FwRect::Count()),valid?1:0);
    }
} late_style;
static unsigned cases=0;static bool same_values=false,raw_twins=false;
static void retain(const std::shared_ptr<FwSTSStyle>& value) {(nan(value->get())?nan_owners:finite_owners).push_back(value);}
static void describe(const STSStyle& value) {
    const CRect r=value.marginRect.get();
    printf("style-value rect=%ld,%ld,%ld,%ld ints=%d,%d,%d,%d,%d,%d,%d,%d font=%d:",r.left,r.top,r.right,r.bottom,
        value.charSet,value.fontWeight,value.fItalic?1:0,value.fUnderline?1:0,value.fStrikeOut?1:0,value.scrAlignment,value.borderStyle,value.relativeTo,value.fontName.GetLength());
    for(int i=0;i<value.fontName.GetLength();++i)printf("%04x",unsigned(static_cast<uint16_t>(value.fontName[i])));
    printf(" colors=");for(unsigned i=0;i<4;++i)printf("%08lx%02x",value.colors[i],unsigned(value.alpha[i]));
    printf(" doubles=");for(auto field:fields)printf("%016llx",static_cast<unsigned long long>(bits(value.*field)));
    printf("\n");
}
static void pair(STSStyle& first,STSStyle& second) {
    unsigned assigned=0;uint64_t assigned_domains=0;
    if(same_values){_clearfp();first=second;assigned=_statusfp();assigned_domains=domains();}
    if(raw_twins){first=second;for(auto field:fields)memcpy(&(first.*field),&(second.*field),8);}
    _clearfp();
    auto a=std::make_shared<FwSTSStyle>(first);const unsigned fa=_statusfp();const uint64_t dfa=domains();_clearfp();
    auto b=std::make_shared<FwSTSStyle>(second);const unsigned fb=_statusfp();const uint64_t dfb=domains();_clearfp();
    auto c=std::make_shared<FwSTSStyle>(*b);const unsigned fc=_statusfp();const uint64_t dfc=domains();_clearfp();
    const STSStyle& av=a->get();const STSStyle& bv=b->get();const unsigned fr=_statusfp();const uint64_t dfr=domains();_clearfp();
    const bool equal=(first==second),self=(second==second);
    const unsigned fe=_statusfp(),flags=fa|fb|fc|fr|fe;const uint64_t dfe=domains();
    printf("style-intern index=%u alias=%u copy_alias=%u value_equal=%u self_equal=%u count=%llu rects=%llu fp=%08x hash=%016llx,%016llx\n",
        cases++,*a==*b?1:0,*b==*c?1:0,equal?1:0,self?1:0,static_cast<unsigned long long>(FwSTSStyle::Count()),
        static_cast<unsigned long long>(FwRect::Count()),flags,static_cast<unsigned long long>(hash_value(first)),static_cast<unsigned long long>(hash_value(second)));
    printf("style-fp index=%u first=%08x second=%08x copy=%08x read=%08x equality=%08x assigned=%08x assigned_domain=%016llx domains=%016llx,%016llx,%016llx,%016llx,%016llx\n",cases-1,fa,fb,fc,fr,fe,assigned,static_cast<unsigned long long>(assigned_domains),static_cast<unsigned long long>(dfa),static_cast<unsigned long long>(dfb),static_cast<unsigned long long>(dfc),static_cast<unsigned long long>(dfr),static_cast<unsigned long long>(dfe));
    describe(av);describe(bv);retain(a);retain(b);retain(c);
}
class StyleDocument : public CSimpleTextSubtitle {
public:
    void show_default() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        const auto value=m_document->DefaultStyle();describe(value);
#else
        describe(m_defaultStyle);
#endif
    }
    void names() const {
        printf("style-map-names");
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        for(size_t i=0;i<GetStyleCount();++i){CString name;if(!GetStyleNameAt(i,name))abort();
#else
        POSITION pos=m_styles.GetStartPosition();while(pos){const CString& name=m_styles.GetNextKey(pos);
#endif
            printf(" ");for(int j=0;j<name.GetLength();++j)printf("%04x",unsigned(uint16_t(name[j])));
        }
        printf("\n");
    }
    size_t count() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetStyleCount();
#else
        return m_styles.GetCount();
#endif
    }
    void show(const wchar_t* name) const {
        STSStyle copy;bool present;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        present=GetNamedStyleCopy(CString(name),copy);
#else
        STSStyle* value=nullptr;present=m_styles.Lookup(CString(name),value)!=FALSE;
        if(present)copy=*value;
#endif
        printf("style-add-name present=%u\n",present?1:0);if(present)describe(copy);
    }
};
static int map_lifecycle() {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 2;
    printf("style-map-provenance pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    unsigned index=0;
    for(unsigned sticky=0;sticky<2;++sticky){
        auto reset=[&](){_clearfp();if(sticky && std::feraiseexcept(FE_DIVBYZERO))abort();return domains();};
        auto report=[&](const char* op,uint64_t before,StyleDocument* doc){
            const unsigned fp=_statusfp();const uint64_t after=domains();
            printf("style-map index=%u sticky=%u op=%s count=%llu fp=%08x domains=%016llx,%016llx\n",index++,sticky,op,
                static_cast<unsigned long long>(doc?doc->count():0),fp,static_cast<unsigned long long>(before),static_cast<unsigned long long>(after));
            if(doc)doc->names();else printf("style-map-names\n");
        };
        uint64_t before=reset();auto doc=new StyleDocument;report("construct",before,doc);
        for(unsigned phase=0;phase<3;++phase)for(unsigned i=0;i<(phase==2?32:128);++i){
            CString name;name.Format(L"Map%04u",i);auto value=new STSStyle;value->fontWeight=400+i+(phase==2?1000:0);
            before=reset();doc->AddStyle(name,value);report(phase==0?"insert":phase==1?"equal":"conflict",before,doc);
        }
        before=reset();doc->Empty();report("empty",before,doc);
        for(unsigned i=0;i<64;++i){
            CString name;name.Format(L"Reuse%04u",i);auto value=new STSStyle;value->fontWeight=400+i;
            before=reset();doc->AddStyle(name,value);report("reuse",before,doc);
        }
        before=reset();doc->Empty();report("empty",before,doc);
        before=reset();delete doc;report("destroy",before,nullptr);
    }
    printf("style-map-complete cases=%u\n",index);return 0;
}
static int add_styles() {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 2;
    printf("style-add-provenance pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,1,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
        0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff0000000000001ull};
    unsigned index=0;
    for(unsigned same=0;same<2;++same)for(auto field:fields)for(auto n:values){
        StyleDocument doc;auto first=new STSStyle;auto second=new STSStyle;
        first->*field=0.0;memcpy(&(second->*field),&n,8);if(same)*first=*second;
        _clearfp();doc.AddStyle(CString(L"Probe"),first);unsigned fa=_statusfp();uint64_t da=domains();
        _clearfp();doc.AddStyle(CString(L"Probe"),second);unsigned fb=_statusfp();uint64_t db=domains();
        printf("style-add index=%u count=%llu first=%08x second=%08x domains=%016llx,%016llx\n",index++,static_cast<unsigned long long>(doc.count()),fa,fb,static_cast<unsigned long long>(da),static_cast<unsigned long long>(db));
        doc.show(L"Probe");doc.show(L"Probe2");
    }
    printf("style-add-complete cases=%u\n",index);return 0;
}
int main() {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 2;
    printf("default-provenance pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,1,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
        0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff0000000000001ull};
    unsigned index=0;
    for(auto field:fields)for(auto n:values){
        StyleDocument source,destination;STSStyle input;memcpy(&(input.*field),&n,8);
        auto report=[&](const char* op,StyleDocument& doc){
            const unsigned status=_statusfp();const uint64_t d=domains();
            printf("default-style index=%u op=%s fp=%08x domains=%016llx\n",index,op,status,static_cast<unsigned long long>(d));doc.show_default();
        };
        _clearfp();source.SetDefaultStyle(input);report("set",source);
        _clearfp();destination.Copy(source);report("copy",destination);
        _clearfp();source.Copy(source);report("self",source);
        _clearfp();source.Empty();report("empty",source);
        ++index;
    }
    printf("default-complete cases=%u\n",index);return 0;
}
