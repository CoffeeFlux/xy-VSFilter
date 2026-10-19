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
    bool copy_named(const wchar_t* name, STSStyle& copy) const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetNamedStyleCopy(CString(name),copy);
#else
        STSStyle* value=nullptr;if(!m_styles.Lookup(CString(name),value)||!value)return false;
        copy=*value;return true;
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
static int map_lifecycle(bool growth=false) {
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
        for(unsigned phase=0;phase<3;++phase)for(unsigned i=0;i<(phase==2?32:phase==0&&growth?512:128);++i){
            CString name;name.Format(L"Map%04u",i);auto value=new STSStyle;value->fontWeight=400+i+(phase==2?1000:0);
            before=reset();doc->AddStyle(name,value);report(phase==0?"insert":phase==1?"equal":"conflict",before,doc);
        }
        before=reset();doc->Empty();report("empty",before,doc);
        for(unsigned i=0;i<64;++i){
            CString name;name.Format(L"Reuse%04u",i);auto value=new STSStyle;value->fontWeight=400+i;
            before=reset();doc->AddStyle(name,value);report("reuse",before,doc);
        }
        if(growth)for(unsigned i=0;i<32;++i){
            CString name;name.Format(L"Reuse%04u",i);auto value=new STSStyle;value->fontWeight=1400+i;
            before=reset();doc->AddStyle(name,value);report("reuse-conflict",before,doc);
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
static int default_styles() {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 2;
    printf("default-provenance pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    const uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,1,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
        0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff0000000000001ull};
    unsigned index=0;
    for(unsigned sticky=0;sticky<2;++sticky)for(auto field:fields)for(auto n:values){
        StyleDocument source,destination;STSStyle input;memcpy(&(input.*field),&n,8);
        auto reset=[&](){_clearfp();if(sticky && std::feraiseexcept(FE_DIVBYZERO))abort();return domains();};
        auto report=[&](const char* op,uint64_t before,StyleDocument& doc){
            const unsigned status=_statusfp();const uint64_t d=domains();
            printf("default-style index=%u sticky=%u op=%s fp=%08x domains=%016llx,%016llx\n",index,sticky,op,status,static_cast<unsigned long long>(before),static_cast<unsigned long long>(d));doc.show_default();
        };
        uint64_t before=reset();source.SetDefaultStyle(input);report("set",before,source);
        before=reset();destination.Copy(source);report("copy",before,destination);
        before=reset();source.Copy(source);report("self",before,source);
        before=reset();source.Empty();report("empty",before,source);
        ++index;
    }
    printf("default-complete cases=%u\n",index);return 0;
}
static int document_rectangles(){
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 2;
    printf("document-rect-provenance pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    auto report=[](const char* stage){printf("document-rect stage=%s rectangles=%llu styles=%llu\n",stage,static_cast<unsigned long long>(FwRect::Count()),static_cast<unsigned long long>(FwSTSStyle::Count()));};
    report("initial");auto source=std::make_unique<StyleDocument>();report("source");
    auto destination=std::make_unique<StyleDocument>();report("destination");
    auto input=std::make_unique<STSStyle>();input->marginRect=CRect(1,2,3,4);report("input");
    source->SetDefaultStyle(*input);report("set-default");input.reset();report("drop-input");
    destination->Copy(*source);report("copy");source->Empty();report("empty-source");
    auto named=new STSStyle;named->marginRect=CRect(5,6,7,8);source->AddStyle(CString(L"Owned"),named);report("add-named");
    destination->Copy(*source);report("copy-named");source->Empty();report("empty-source-again");
    destination->Empty();report("empty-destination");source.reset();report("drop-source");destination.reset();report("drop-destination");
    printf("document-rect-complete cases=14\n");return 0;
}

static int document_rectangle_history() {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 2;
    printf("document-history-provenance pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    unsigned phases=0;
    auto report=[&](const char* stage){++phases;printf("document-history stage=%s rectangles=%llu styles=%llu\n",stage,static_cast<unsigned long long>(FwRect::Count()),static_cast<unsigned long long>(FwSTSStyle::Count()));};
    report("initial");auto source=std::make_unique<StyleDocument>();report("source");
    auto destination=std::make_unique<StyleDocument>();report("destination");
    auto input=std::make_unique<STSStyle>();input->marginRect=CRect(1,2,3,4);report("input");
    source->SetDefaultStyle(*input);report("set-default");
    auto alias=std::make_unique<STSStyle>(*input);report("alias-input");
    input.reset();report("drop-input");
    source->AddStyle(CString(L"Owned"),new STSStyle(*alias));report("add-alias");
    alias.reset();report("drop-alias");
    auto incoming=new STSStyle;incoming->marginRect=CRect(5,6,7,8);
    source->AddStyle(CString(L"Owned"),incoming);report("conflict");
    auto snapshot=std::make_unique<STSStyle>();if(!source->copy_named(L"Owned",*snapshot))return 3;
    report("snapshot");source->AddStyle(CString(L"Owned"),new STSStyle(*snapshot));report("equal-conflict");
    auto interned=std::make_unique<FwSTSStyle>(*snapshot);report("interned");
    destination->Copy(*source);report("copy");source->Copy(*source);report("self-copy-source");
    source.reset();report("drop-source");destination->Empty();report("empty-destination");
    destination.reset();report("drop-destination");snapshot.reset();report("drop-snapshot");
    auto recreated=std::make_unique<StyleDocument>();report("recreate");
    recreated->AddStyle(CString(L"Owned"),new STSStyle(interned->get()));report("add-retained");
    interned.reset();report("drop-interned");recreated->Copy(*recreated);report("self-copy-recreated");
    recreated.reset();report("drop-recreated");
    printf("document-history-complete cases=%u\n",phases);return 0;
}

int main(int argc,char** argv) {
    if(argc>1&&!strcmp(argv[1],"document-rectangle-history"))return document_rectangle_history();
    if(argc>1&&!strcmp(argv[1],"document-rectangles"))return document_rectangles();
    if(argc>1&&!strcmp(argv[1],"default-style"))return default_styles();
    if(argc>1&&!strcmp(argv[1],"map-lifecycle"))return map_lifecycle();
    if(argc>1&&!strcmp(argv[1],"map-growth"))return map_lifecycle(true);
    same_values=argc>1&&!strcmp(argv[1],"same-values");
    raw_twins=argc>1&&!strcmp(argv[1],"raw-twins");
    if(argc>1&&!strcmp(argv[1],"add-style"))return add_styles();
    if(argc>1 && !strcmp(argv[1],"lifecycle")) {
        {
            FwSTSStyle a,b(a),c(std::move(a)),empty(std::move(a));
            printf("style-move copy=%u empty=%u distinct=%u count=%llu\n",b==c?1:0,a==empty?1:0,a!=c?1:0,static_cast<unsigned long long>(FwSTSStyle::Count()));
            a=b;empty=std::move(c);
            printf("style-move restored=%u source_empty=%u count=%llu\n",a==empty?1:0,c!=empty?1:0,static_cast<unsigned long long>(FwSTSStyle::Count()));
        }
        _clearfp();if(std::feraiseexcept(FE_DIVBYZERO))return 2;const unsigned before=_statusfp();const uint64_t before_domains=domains();
        {STSStyle s;FwSTSStyle a(s),b(a);const STSStyle& snapshot=b.get();if(snapshot.fontSize!=18)return 3;}
        const unsigned after=_statusfp();const uint64_t after_domains=domains();
        printf("style-sticky before=%08x after=%08x domains=%016llx,%016llx count=%llu rects=%llu\n",before,after,static_cast<unsigned long long>(before_domains),static_cast<unsigned long long>(after_domains),static_cast<unsigned long long>(FwSTSStyle::Count()),static_cast<unsigned long long>(FwRect::Count()));
        {STSStyle s;s.fontName=L"late-style";late_style.value=std::make_shared<FwSTSStyle>(s);}
        printf("style-lifecycle-complete count=%llu\n",static_cast<unsigned long long>(FwSTSStyle::Count()));
        return 0;
    }
    if(argc>1 && !same_values && !raw_twins) {
        // No lookup occurs after release. Count() itself never dereferences keys.
        STSStyle value;const uint64_t n=0x7ff8000000000001ull;memcpy(&value.fontSize,&n,8);
        auto owner=std::make_shared<FwSTSStyle>(value);
        const size_t before=FwSTSStyle::Count();owner.reset();
        printf("style-released-nan pointer_bits=%u before=%llu after=%llu\n",unsigned(sizeof(void*)*8),
            static_cast<unsigned long long>(before),static_cast<unsigned long long>(FwSTSStyle::Count()));
        return 0;
    }
    printf("style-provenance pointer_bits=%u ansi=%u oem=%u\n",unsigned(sizeof(void*)*8),GetACP(),GetOEMCP());
    const uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,1,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
        0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff0000000000001ull};
    for(auto field:fields)for(auto n:values) {
        STSStyle a;a.charSet=1000+cases;a.*field=0.0;STSStyle b(a);memcpy(&(b.*field),&n,8);pair(a,b);
    }
    for(unsigned i=0;i<8;++i) {
        STSStyle a;a.charSet=1000+cases;
        const wchar_t raw[]={i<4?L'A':wchar_t(0xd800),0,i%2?L'X':wchar_t(0xffff)};
        a.fontName=CStringW(raw,i%2?3:1);STSStyle b(a);b.fontName=CStringW(raw,i%2?1:3);
        pair(a,b);
    }
    for(unsigned i=0;i<8;++i) {
        STSStyle a;a.charSet=1000+cases;STSStyle b(a);
        b.marginRect=CRect(i%2?INT32_MIN:20,i%3?INT32_MAX:20,int(i)-4,-int(i));pair(a,b);
    }
    _clearfp();finite_owners.clear();const unsigned released_flags=_statusfp();
    printf("style-release-fp fp=%08x domains=%016llx\n",released_flags,static_cast<unsigned long long>(domains()));
    printf("style-final finite_released=1 retained_nan=%llu styles=%llu rects=%llu\n",static_cast<unsigned long long>(nan_owners.size()),
        static_cast<unsigned long long>(FwSTSStyle::Count()),static_cast<unsigned long long>(FwRect::Count()));
    printf("style-intern-complete cases=%u\n",cases);
    // Deliberately retain NaN owners until process reclamation. Sequential NaN
    // destruction/lookup can dereference freed native keys; no parity claim.
    fflush(stdout);_exit(0);
}
