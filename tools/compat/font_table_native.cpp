// SPDX-License-Identifier: GPL-2.0-or-later
// Cleanup key-owning interner, with the actual STSStyleBase hash/equality.
// Payloads observe construction/destruction; they deliberately do not call GDI.
#include <stdafx.h>
#include <STS.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>

static unsigned tag(const STSStyleBase& s) { return unsigned(static_cast<uint16_t>(s.fontName[2])); }
struct Key {
    STSStyleBase value;
    explicit Key(const STSStyleBase& s):value(s){}
    bool operator==(const Key& other)const {
        bool result=value==other.value;
        printf("eq %u %u %u\n",tag(value),tag(other.value),result?1:0);return result;
    }
};
static size_t hash_value(const Key& s) { return hash_value(s.value); }
struct Payload {
    STSStyleBase key;
    explicit Payload(const Key& s):key(s.value) {printf("create %u\n",tag(key));}
    ~Payload(){printf("destroy %u\n",tag(key));}
};
struct ActualPayload { STSStyleBase key;explicit ActualPayload(const STSStyleBase& k):key(k){} };
typedef XyInternedByKey<Key,Payload> Logged;
typedef XyInternedByKey<STSStyleBase,ActualPayload> Actual;
static std::shared_ptr<Logged> slots[704];
static std::shared_ptr<Actual> actual[704];
static unsigned step=0,serial=0;
static void octet(unsigned n){printf("%02x",n&255);}
static void integer(uint64_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)octet(unsigned(n>>(8*i)));}
static void wire(const STSStyleBase& k){
    integer(k.charSet,4);integer(k.fontWeight,4);octet(k.fItalic);octet(k.fUnderline);octet(k.fStrikeOut);octet(0);
    integer(k.fontName.GetLength(),4);uint64_t bits;memcpy(&bits,&k.fontSize,8);integer(bits,8);
    for(int i=0;i<k.fontName.GetLength();++i)integer(static_cast<uint16_t>(k.fontName[i]),2);
}
static void state(unsigned slot){
    if(Logged::Count()!=Actual::Count())abort();
    for(unsigned i=0;i<704;++i)if(slots[i]){
        if(tag(slots[i]->get().key)!=tag(actual[i]->get().key))abort();
        if(slots[slot] && ((&slots[i]->get()==&slots[slot]->get())!=(&actual[i]->get()==&actual[slot]->get())))abort();
    }
    printf("state %u %u %llu ",step++,slot,static_cast<unsigned long long>(Logged::Count()));
    for(unsigned i=0;i<704;i+=8){unsigned mask=0;for(unsigned j=0;j<8;++j)if(slots[slot]&&slots[i+j]&&&slots[slot]->get()==&slots[i+j]->get())mask|=1u<<j;octet(mask);}
    printf(" ");if(slots[slot])wire(slots[slot]->get().key);else printf("-");printf("\n");
}
static void insert(unsigned slot,unsigned variant,uint64_t bits=0x4034000000000000ull,unsigned flags=0,unsigned name=65){
    STSStyle init;STSStyleBase s=init;
    wchar_t raw[]={wchar_t(name),0,wchar_t(++serial)};s.fontName=CStringW(raw,3);
    s.charSet=variant;s.fontWeight=int(flags>>3);s.fItalic=bool(flags&1);s.fUnderline=bool(flags&2);s.fStrikeOut=bool(flags&4);memcpy(&s.fontSize,&bits,8);
    printf("input %u %u %016llx ",step,slot,static_cast<unsigned long long>(hash_value(s)));wire(s);printf("\n");
    actual[slot]=std::make_shared<Actual>(s);slots[slot]=std::make_shared<Logged>(Key(s));state(slot);
}
static void copy(unsigned to,unsigned from){printf("copy %u %u %u\n",step,to,from);actual[to]=actual[from];slots[to]=slots[from];state(to);}
static void drop(unsigned slot){printf("drop %u %u\n",step,slot);actual[slot].reset();slots[slot].reset();state(slot);}
int main(){
    printf("font-table pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    // Unlike the style table, expired NaN keys remain safe to compare and rehash.
    insert(0,0,0x7ff8000000000001ull);drop(0);
    for(unsigned i=0;i<640;++i)insert(i,1+i);
    for(unsigned i=0;i<64;++i)copy(640+i,i*7%640);
    for(unsigned i=0;i<640;i+=3)drop(i);
    for(unsigned i=0;i<128;++i)insert(i,1+i*7%640);
    for(unsigned i=0;i<704;++i)drop((i*193)%704);
    const uint64_t sizes[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,1,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,
        0x7ff8000000000001ull,0xfff8000000000001ull,0x7ff0000000000001ull};
    for(auto bits:sizes){insert(0,0,bits);insert(1,0,bits);copy(2,0);drop(0);drop(1);drop(2);}
    for(unsigned flags=0;flags<24;++flags){insert(0,0,0x4034000000000000ull,flags);insert(1,0,0x4034000000000000ull,flags);drop(0);drop(1);}
    const unsigned names[]={0,65,0xd800,0xdc00,0xffff};
    for(auto name:names){insert(0,0,0x4034000000000000ull,0,name);insert(1,0,0x4034000000000000ull,0,name);drop(0);drop(1);}
    // Force another growth with expired NaN keys still owned by the table.
    for(unsigned i=0;i<640;++i)insert(i,10000+i);
    for(unsigned i=0;i<704;++i)drop(i);
    printf("font-table-complete steps=%u fonts=%llu\n",step,static_cast<unsigned long long>(Logged::Count()));
}
