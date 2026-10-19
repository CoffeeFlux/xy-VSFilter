// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup style interner observations. NaN keys stay alive: the native
// deleter may leave an expired raw key, so arbitrary post-release lookup is NOT
// included as passing evidence. A separate child observes count after one drop.
#include <stdafx.h>
#include <STS.h>
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
static unsigned cases=0;
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
    printf("style-fp index=%u first=%08x second=%08x copy=%08x read=%08x equality=%08x domains=%016llx,%016llx,%016llx,%016llx,%016llx\n",cases-1,fa,fb,fc,fr,fe,static_cast<unsigned long long>(dfa),static_cast<unsigned long long>(dfb),static_cast<unsigned long long>(dfc),static_cast<unsigned long long>(dfr),static_cast<unsigned long long>(dfe));
    describe(av);describe(bv);retain(a);retain(b);retain(c);
}
int main(int argc,char** argv) {
    if(argc>1 && !strcmp(argv[1],"lifecycle")) {
        {
            FwSTSStyle a,b(a),c(std::move(a)),empty(std::move(a));
            printf("style-move copy=%u empty=%u distinct=%u count=%llu\n",b==c?1:0,a==empty?1:0,a!=c?1:0,static_cast<unsigned long long>(FwSTSStyle::Count()));
            a=b;empty=std::move(c);
            printf("style-move restored=%u source_empty=%u count=%llu\n",a==empty?1:0,c!=empty?1:0,static_cast<unsigned long long>(FwSTSStyle::Count()));
        }
        _clearfp();if(std::feraiseexcept(FE_DIVBYZERO))return 2;const unsigned before=_statusfp();
        {STSStyle s;FwSTSStyle a(s),b(a);const STSStyle& snapshot=b.get();if(snapshot.fontSize!=18)return 3;}
        const unsigned after=_statusfp();
        printf("style-sticky before=%08x after=%08x count=%llu rects=%llu\n",before,after,static_cast<unsigned long long>(FwSTSStyle::Count()),static_cast<unsigned long long>(FwRect::Count()));
        {STSStyle s;s.fontName=L"late-style";late_style.value=std::make_shared<FwSTSStyle>(s);}
        printf("style-lifecycle-complete count=%llu\n",static_cast<unsigned long long>(FwSTSStyle::Count()));
        return 0;
    }
    if(argc>1) {
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
    printf("style-release-fp fp=%08x\n",released_flags);
    printf("style-final finite_released=1 retained_nan=%llu styles=%llu rects=%llu\n",static_cast<unsigned long long>(nan_owners.size()),
        static_cast<unsigned long long>(FwSTSStyle::Count()),static_cast<unsigned long long>(FwRect::Count()));
    printf("style-intern-complete cases=%u\n",cases);
    // Deliberately retain NaN owners until process reclamation. Sequential NaN
    // destruction/lookup can dereference freed native keys; no parity claim.
    fflush(stdout);_exit(0);
}
