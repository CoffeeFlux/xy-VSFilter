// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup XyInterned template, instrumenting only its value comparator.
// A parallel actual FwSTSStyle table checks the instrumented wrapper's aliases.
#include <stdafx.h>
#include <STS.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>
#include <process.h>

static unsigned tag(const STSStyle& s) { return unsigned(static_cast<uint16_t>(s.fontName[2])); }
struct LoggedStyle {
    STSStyle value;
    explicit LoggedStyle(const STSStyle& s):value(s){}
    bool operator==(const LoggedStyle& other)const {
        const bool result=value==other.value;
        printf("eq %u %u %u\n",tag(value),tag(other.value),result?1:0);
        return result;
    }
};
static size_t hash_value(const LoggedStyle& s) {return hash_value(s.value);}
typedef XyInterned<LoggedStyle> Logged;
static std::shared_ptr<Logged> slots[704];
static std::shared_ptr<FwSTSStyle> actual[704];
static unsigned step=0, serial=0;
static void write_byte(unsigned n) {printf("%02x",n&255);}
static void integer(uint64_t n,unsigned width) {for(unsigned i=0;i<width;++i)write_byte(unsigned(n>>(i*8)));}
static void wire(const STSStyle& s) {
    integer(s.charSet,4);integer(s.fontWeight,4);integer(s.scrAlignment,4);integer(s.borderStyle,4);integer(s.relativeTo,4);
    CRect r=s.marginRect.get();integer(r.left,4);integer(r.top,4);integer(r.right,4);integer(r.bottom,4);
    for(unsigned i=0;i<4;++i)integer(s.colors[i],4);
    for(unsigned i=0;i<4;++i)write_byte(s.alpha[i]);
    write_byte(s.fItalic);write_byte(s.fUnderline);write_byte(s.fStrikeOut);
    for(unsigned i=0;i<5;++i)write_byte(0);
    integer(s.fontName.GetLength(),4);integer(0,4);
    const double* fields[]={&s.fontSize,&s.outlineWidthX,&s.outlineWidthY,&s.shadowDepthX,&s.shadowDepthY,
        &s.fontScaleX,&s.fontScaleY,&s.fontSpacing,&s.fBlur,&s.fGaussianBlur,&s.fontAngleZ,&s.fontAngleX,&s.fontAngleY,&s.fontShiftX,&s.fontShiftY};
    for(auto p:fields){uint64_t bits;memcpy(&bits,p,8);integer(bits,8);}
    for(int i=0;i<s.fontName.GetLength();++i)integer(static_cast<uint16_t>(s.fontName[i]),2);
}
static void state(unsigned slot) {
    if(Logged::Count()!=FwSTSStyle::Count())abort();
    // All held aliases must agree with the actual renderer specialization.
    for(unsigned i=0;i<704;++i)if(slots[i]) {
        if(tag(slots[i]->get().value)!=tag(actual[i]->get()))abort();
        if(slots[slot] && ((*slots[i]==*slots[slot])!=(*actual[i]==*actual[slot])))abort();
    }
    printf("state %u %u %llu %llu ",step++,slot,static_cast<unsigned long long>(Logged::Count()),static_cast<unsigned long long>(FwRect::Count()));
    for(unsigned i=0;i<704;i+=8) {
        unsigned mask=0;for(unsigned j=0;j<8;++j)if(slots[slot] && slots[i+j] && *slots[slot]==*slots[i+j])mask|=1u<<j;
        write_byte(mask);
    }
    printf(" ");
    if(slots[slot])wire(slots[slot]->get().value);else printf("-");
    printf("\n");
}
static void insert(unsigned slot,unsigned variant,unsigned kind=0) {
    {
        STSStyle s;
        const wchar_t name[]={L'A',0,wchar_t(++serial)};s.fontName=CStringW(name,3);
        s.charSet=1+variant%32;s.colors[1]=variant;s.marginRect=CRect(variant%5,20,20,20);
        if(kind==1)s.fontSize=0.0;
        if(kind==2){const uint64_t n=0x8000000000000000ull;memcpy(&s.fontSize,&n,8);}
        if(kind==3){const uint64_t n=0x7ff8000000000001ull;memcpy(&s.fontSize,&n,8);}
        printf("input %u %u %016llx ",step,slot,static_cast<unsigned long long>(hash_value(s)));wire(s);printf("\n");
        actual[slot]=std::make_shared<FwSTSStyle>(s);
        slots[slot]=std::make_shared<Logged>(LoggedStyle(s));
    }
    state(slot);
}
static void supplied(unsigned slot,const STSStyle& input) {
    {
        STSStyle s(input);const wchar_t name[]={L'A',0,wchar_t(++serial)};s.fontName=CStringW(name,3);
        printf("input %u %u %016llx ",step,slot,static_cast<unsigned long long>(hash_value(s)));wire(s);printf("\n");
        actual[slot]=std::make_shared<FwSTSStyle>(s);slots[slot]=std::make_shared<Logged>(LoggedStyle(s));
    }
    state(slot);
}
static void copy(unsigned to,unsigned from) {
    printf("copy %u %u %u\n",step,to,from);
    actual[to]=actual[from];slots[to]=slots[from];state(to);
}
static void drop(unsigned slot) {
    printf("drop %u %u\n",step,slot);
    actual[slot].reset();slots[slot].reset();state(slot);
}
int main(int argc,char**) {
    if(argc>1) {
        printf("style-table pointer_bits=%u mode=edges\n",unsigned(sizeof(void*)*8));
        double STSStyle::* fields[]={&STSStyle::fontSize,&STSStyle::outlineWidthX,&STSStyle::outlineWidthY,
            &STSStyle::shadowDepthX,&STSStyle::shadowDepthY,&STSStyle::fontScaleX,&STSStyle::fontScaleY,
            &STSStyle::fontSpacing,&STSStyle::fBlur,&STSStyle::fGaussianBlur,&STSStyle::fontAngleZ,
            &STSStyle::fontAngleX,&STSStyle::fontAngleY,&STSStyle::fontShiftX,&STSStyle::fontShiftY};
        const uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,1,
            0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
            0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff0000000000001ull};
        bool nonreflexive[360]={};unsigned index=0;
        for(auto field:fields)for(auto bits:values) {
            STSStyle a;a.charSet=1000+index;a.*field=0.0;STSStyle b(a);memcpy(&(b.*field),&bits,8);
            supplied(index*2,a);supplied(index*2+1,b);
            nonreflexive[index*2+1]=(bits&0x7ff0000000000000ull)==0x7ff0000000000000ull && (bits&0xfffffffffffffull);
            ++index;
        }
        unsigned held=0;
        for(unsigned i=0;i<360;++i)if(nonreflexive[i])copy(360+held++,i);
        for(unsigned i=0;i<360;++i)if(!nonreflexive[i])drop(i);
        printf("style-table-complete steps=%u styles=%llu rects=%llu\n",step,static_cast<unsigned long long>(Logged::Count()),static_cast<unsigned long long>(FwRect::Count()));
        // Keep all NaN keys alive; never execute an expired-key lookup on exit.
        fflush(stdout);_exit(0);
    }
    printf("style-table pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    for(unsigned i=0;i<640;++i)insert(i,i);
    for(unsigned i=0;i<64;++i)copy(640+i,i*7%640);
    for(unsigned i=0;i<640;i+=3)drop(i);
    for(unsigned i=0;i<128;++i)insert(i,i*7%640);
    for(unsigned i=0;i<704;++i)drop((i*193)%704);
    insert(0,1000,1);insert(1,1000,2);copy(2,0);drop(0);drop(1);drop(2);
    insert(0,2000,3);copy(1,0);drop(0);drop(1);
    // No lookup after the final non-reflexive key's release.
    printf("style-table-complete steps=%u styles=%llu rects=%llu\n",step,static_cast<unsigned long long>(Logged::Count()),static_cast<unsigned long long>(FwRect::Count()));
    return 0;
}
