// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup flyweight/cache methods, including CString's raw stored units.
#include <stdafx.h>
#include <flyweight_base_types.h>
#include <cstdio>
#include <vector>
#include <memory>
#include <stdexcept>
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include <RustDocumentOwner.h>
#include <thread>
#include <type_traits>
static_assert(std::is_trivially_destructible<XyRust::DocumentDetail::Gate>::value,"gate must survive late cache destruction");
static void gate_controls() {
    unsigned count=0;
    auto worker=[&]() {
        for(unsigned i=0;i<10000;++i) {
            XyRust::DocumentDetail::Loan outer;
            XyRust::DocumentDetail::Loan inner;
            ++count;
        }
    };
    std::thread first(worker),second(worker);worker();first.join();second.join();
    if(count!=30000)throw std::runtime_error("recursive gate serialization");
}
#endif

// Constructed before main. Its destructor exercises owner release after local
// statics (including the old admission gate) would have been destroyed.
struct LateOwner {
    std::shared_ptr<XyFwStringW> value;
    ~LateOwner() {
        if(!value)return;
        value.reset();
        printf("string-shutdown released=1\n");
    }
};
static LateOwner late;

static unsigned sequence=0;
static std::vector<std::shared_ptr<XyFwStringW>> held(12);
static CStringW raw(std::initializer_list<wchar_t> value) {
    return CStringW(value.begin(),static_cast<int>(value.size()));
}
static void observe(unsigned operation,unsigned slot) {
    auto cache=XyFwStringW::GetCacher();
    printf("string-step index=%u op=%u slot=%u max=%llu count=%llu hits=%llu queries=%llu\n",sequence++,operation,slot,
        static_cast<unsigned long long>(cache->GetMaxItemNum()),static_cast<unsigned long long>(cache->GetCurItemNum()),
        static_cast<unsigned long long>(cache->GetCacheHitCount()),static_cast<unsigned long long>(cache->GetQueryCount()));
    for(unsigned i=0;i<held.size();++i) {
        printf("held slot=%u id=%llu units=",i,held[i]?static_cast<unsigned long long>(held[i]->GetId()):0);
        const CStringW value=held[i]?held[i]->Get():CStringW();
        printf("%d hex=",value.GetLength());
        for(int u=0;u<value.GetLength();++u)printf("%04x",static_cast<unsigned>(static_cast<uint16_t>(value[u])));
        printf("\n");
    }
}
static void add(unsigned slot,const CStringW& value) {
    held[slot]=std::make_shared<XyFwStringW>(new CStringW(value));observe(0,slot);
}
int main() {
    try {
        printf("string-provenance pointer_bits=%u ansi=%u oem=%u\n",unsigned(sizeof(void*)*8),GetACP(),GetOEMCP());
        auto cache=XyFwStringW::GetCacher();observe(1,0);
        const CStringW values[]={CStringW(),raw({0}),raw({0,L'X'}),raw({L'A',0,L'B'}),CStringW(L"A"),
            raw({L'A',0,L'C'}),CStringW(L"a"),raw({0xd800}),raw({0xdc00}),raw({0xffff}),
            raw({L'A',L'B'}),raw({L'B',L'!'}),CStringW(L" "),raw({L' ',0,L'x'}),CStringW(L"\t\r\n")};
        for(unsigned pass=0;pass<5;++pass) {
            cache->RemoveAll(pass%2==0);observe(2,pass);
            const size_t caps[]={1,2,3,17,16384};cache->SetMaxItemNum(caps[pass],pass%2!=0);observe(3,pass);
            for(unsigned repeat=0;repeat<3;++repeat) for(unsigned i=0;i<15;++i) {
                unsigned at=(i*7+repeat)%12;add(at,values[(i+repeat*3)%15]);
                if(i%5==0) {held[(at+1)%12]=std::make_shared<XyFwStringW>(*held[at]);observe(4,(at+1)%12);}
                if(i%7==0) { *held[at]=*held[at];observe(5,at); }
            }
        }
        // Exceed the real default capacity while retaining old handles.
        cache->RemoveAll(true);cache->SetMaxItemNum(16384);observe(6,0);
        for(unsigned i=0;i<16410;++i) {
            CStringW value;value.Format(L"churn-%05u",i);
            XyFwStringW item(new CStringW(value));
            if(i<12)held[i]=std::make_shared<XyFwStringW>(item);
            if(i%1000==0 || i>=16380)observe(7,i);
        }
        for(unsigned i=0;i<12;++i) {const CStringW old=held[i]->Get();add(i,old);}
        // Hits move to the MRU head; retained handles survive all cache removals.
        cache->SetMaxItemNum(3,true);observe(8,0);
        add(0,L"one");add(1,L"two");add(2,L"three");add(3,L"one");add(4,L"four");add(5,L"two");
        cache->RemoveTail();observe(9,0);cache->RemoveAll();observe(10,0);
        cache->SetMaxItemNum(0);observe(11,0); // No construction at zero: native returns a freed list node.
        cache->RemoveTail();observe(12,0);cache->SetMaxItemNum(16384,true);observe(13,0);
        for(unsigned i=0;i<12;++i){held[i].reset();observe(14,i);}
        late.value=std::make_shared<XyFwStringW>(new CStringW(L"late-owner"));
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        gate_controls();
#endif
        printf("string-complete steps=%u\n",sequence);return 0;
    } catch(const std::exception& e) {fprintf(stderr,"unresolved: %s\n",e.what());return 2;}
}
