// SPDX-License-Identifier: GPL-2.0-or-later
// Windows reference primitive observations only; not a Rust parity test.
#include <windows.h>
#include <atlbase.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atltypes.h>
#include <dshow.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <cfenv>
#include <cstdint>
#include <cstddef>
#include <float.h>

// The actual pinned STSEntry declaration is extracted below. Native Sort uses
// qsort on these CString-bearing records; only bounded comparator inputs run.
struct LSub { int idx, layer, readorder; };
#include "document_windows_extracted.inc"

// Style pointees are identity tokens only; map key/value/bucket layout uses
// the native CString + STSStyle* types. No style semantics are exercised.
struct STSStyle { int identity; };
typedef ATL::CAtlMap<CString, STSStyle*, ATL::CStringElementTraits<CString> > ProbeMap;
static void dump_map(const char* label, const ProbeMap& map) {
    printf("map %s %zu", label, static_cast<size_t>(map.GetCount()));
    POSITION pos = map.GetStartPosition();
    while (pos) {
        const ProbeMap::CPair* pair = map.GetNext(pos);
        printf(" %d:%d:", pair->m_value->identity, pair->m_key.GetLength());
        for (int i=0; i<pair->m_key.GetLength(); ++i) printf("%04x", static_cast<unsigned>(pair->m_key[i]));
    }
    puts("");
}
static CString key(int n) { CString s; s.Format(L"Style%04d",n); return s; }
static void maps() {
    for (int count : {0,1,16,17,18,32,64,256,1024}) {
        ProbeMap map;
        std::vector<STSStyle> values(count);
        for(int i=0;i<count;++i) { values[i].identity=i; map[key(i)]=&values[i]; }
        dump_map("insert",map);
        for(int i=0;i<count;i+=3) map.RemoveKey(key(i));
        dump_map("remove",map);
        for(int i=0;i<count;i+=3) map[key(i)]=&values[i];
        dump_map("reinsert",map);
        ProbeMap copy;
        POSITION pos=map.GetStartPosition();
        while(pos) { const ProbeMap::CPair* p=map.GetNext(pos); copy[p->m_key]=p->m_value; }
        dump_map("ordered-copy",copy);
    }
    const wchar_t raw[]={L'A',0,L'B',0};
    CString a(raw,3), b(L"A"), c(raw,3);
    printf("cstring %d %d %d %d %d %d\n",a.GetLength(),b.GetLength(),a==b,a==c,a.Compare(b),a.Compare(c));
    STSStyle first={1}, second={2};
    ProbeMap map; map[a]=&first; map[b]=&second; dump_map("nul",map);
}
static void cstring_check(bool condition, const char* operation) {
    if(!condition) { fprintf(stderr,"CString matrix unresolved: %s\n",operation); std::exit(2); }
}
static CString raw_cstring(const std::vector<uint16_t>& units) {
    static_assert(sizeof(wchar_t)==2,"Windows code-unit contract");
    if(units.empty()) return CString();
    std::vector<wchar_t> wide;
    for(uint16_t unit:units) wide.push_back(static_cast<wchar_t>(unit));
    return CString(wide.data(),static_cast<int>(wide.size()));
}
static void print_cstring_units(const CString& value) {
    printf(" %d:",value.GetLength());
    for(int i=0;i<value.GetLength();++i) printf("%04x",static_cast<unsigned>(static_cast<uint16_t>(value[i])));
}
static void cstring_matrix() {
    // Actual Unicode CString equality/assignment, including logical lengths
    // that extend past the first NUL. No style-map/naming behavior is modeled.
    const std::vector<std::vector<uint16_t>> cases = {
        {},{0},{0,0xd800},{65},{65,0},{65,0,66},{65,0,67},{97},
        {65,66},{0xd800},{0xd800,0,0xdc00},{0xdc00},{0xd801},
        {0xd800,1},{0xffff},{0xffff,0}
    };
    size_t comparisons=0,assignments=0;
    for(size_t i=0;i<cases.size();++i) {
        CString original=raw_cstring(cases[i]);
        CString assigned(L"previous destination contents");
        assigned=original;
        cstring_check(assigned.GetLength()==static_cast<int>(cases[i].size()),"assignment length");
        for(size_t unit=0;unit<cases[i].size();++unit)
            cstring_check(static_cast<uint16_t>(assigned[static_cast<int>(unit)])==cases[i][unit],"assignment code units");
        printf("cstring-assignment %zu",i);print_cstring_units(original);print_cstring_units(assigned);puts("");++assignments;
        for(size_t j=0;j<cases.size();++j) {
            CString other=raw_cstring(cases[j]);
            const bool equal=original==other;
            const int comparison=original.Compare(other);
            const auto left_end=std::find(cases[i].begin(),cases[i].end(),uint16_t(0));
            const auto right_end=std::find(cases[j].begin(),cases[j].end(),uint16_t(0));
            const bool prefix_equal=std::vector<uint16_t>(cases[i].begin(),left_end)==std::vector<uint16_t>(cases[j].begin(),right_end);
            cstring_check(equal==prefix_equal && (comparison==0)==equal,"first-NUL equality");
            // Exercise pinned AddStyle's predicate/assignment shape using actual CString.
            CString renamed=original;
            const std::vector<uint16_t> replacement_units={0xdcff,0,0xd800,78};
            const CString replacement=raw_cstring(replacement_units);
            if(renamed==other) renamed=replacement;
            const auto& expected=equal?replacement_units:cases[i];
            cstring_check(renamed.GetLength()==static_cast<int>(expected.size()),"rename length");
            for(size_t unit=0;unit<expected.size();++unit)
                cstring_check(static_cast<uint16_t>(renamed[static_cast<int>(unit)])==expected[unit],"rename code units");
            printf("cstring-matrix %zu %zu %d %d",i,j,equal,comparison);print_cstring_units(renamed);puts("");++comparisons;
        }
    }
    printf("cstring-matrix-summary %zu %zu\n",comparisons,assignments);
}
static void sorts() {
    for(int count=0;count<=7;++count) {
        std::vector<int> ids; for(int i=0;i<count;++i) ids.push_back(i);
        do {
            for(int pattern=0;pattern<3;++pattern) {
                std::vector<STSEntry> entries;
                std::vector<LSub> layers;
                for(int id:ids) {
                    const int k=pattern==0?0:pattern==1?id%2:id%3;
                    STSEntry entry;
                    entry.str.Format(L"entry-%d",id); entry.fUnicode=true;
                    entry.style=L"Default"; entry.actor.Format(L"actor-%d",id); entry.effect=L"effect";
                    entry.marginRect=CRect(id,0,0,0); entry.start=k;entry.end=k+1;
                    entry.layer=pattern==2?k:0;entry.readorder=pattern==1?k:0;
                    entries.push_back(entry);
                    layers.push_back({id,pattern==2?k:0,pattern==1?k:0});
                }
                for(int mode=0;mode<2;++mode) {
                    auto sorted=entries;
                    if(!sorted.empty()) qsort(sorted.data(),sorted.size(),sizeof(STSEntry),mode?comp2:comp1);
                    printf("sort %d %d %d input",count,pattern,mode);
                    for(int id:ids) printf(" %d",id);
                    printf(" output");for(const auto& e:sorted) {
                        printf(" [id=%ld start=%lld end=%lld layer=%d order=%d unicode=%d margins=%ld,%ld,%ld,%ld text=%ls style=%ls actor=%ls effect=%ls]",
                            e.marginRect.left,e.start,e.end,e.layer,e.readorder,e.fUnicode,
                            e.marginRect.left,e.marginRect.top,e.marginRect.right,e.marginRect.bottom,
                            e.str.GetString(),e.style.GetString(),e.actor.GetString(),e.effect.GetString());
                    }puts("");
                }
                if(!layers.empty()) qsort(layers.data(),layers.size(),sizeof(LSub),lscomp);
                printf("lsort %d %d input",count,pattern);for(int id:ids)printf(" %d",id);
                printf(" output");for(const auto& e:layers)printf(" %d",e.idx);puts("");
            }
        } while(std::next_permutation(ids.begin(),ids.end()));
    }
}
static uint64_t bits(double d) { uint64_t b; memcpy(&b,&d,sizeof b); return b; }
static void numeric() {
    // Finite inputs and bounded quotients only. NaN/infinity/zero fps and
    // llround out-of-range inputs are unresolved and deliberately not executed.
    for(long long input : {-9007199254740991LL,-10000001LL,-3LL,-1LL,0LL,1LL,3LL,10000001LL,9007199254740991LL}) {
        for(double fps : {-60.0,-23.976,0.5,1.0,23.976,24.0,25.0,29.97,60.0,10000000.0}) {
            for(int mode=0;mode<2;++mode) {
                const double value=extracted_value(mode,input,fps);
                if(!std::isfinite(value) || std::fabs(value)>=9223372036854775808.0) continue;
                errno=0; std::feclearexcept(FE_ALL_EXCEPT); _clearfp();
                const long long result=extracted_round(mode,input,fps);
                const int saved_errno=errno;
                const int flags=std::fetestexcept(FE_ALL_EXCEPT);
                const unsigned status=_statusfp();
                printf("numeric %d %lld %016llx %016llx %016llx %d %d %x\n",mode,input,
                    static_cast<unsigned long long>(bits(fps)),static_cast<unsigned long long>(bits(value)),
                    static_cast<unsigned long long>(result),saved_errno,flags,status);
            }
        }
    }
    for(double d : {-2.5,-1.5,-0.5,-0.0,0.0,0.5,1.5,2.5}) {
        errno=0;std::feclearexcept(FE_ALL_EXCEPT);_clearfp();
        const long long result=std::llround(d); const int e=errno;
        const int f=std::fetestexcept(FE_ALL_EXCEPT);const unsigned st=_statusfp();
        printf("round %016llx %016llx %d %d %x\n",static_cast<unsigned long long>(bits(d)),static_cast<unsigned long long>(result),e,f,st);
    }
}
int main() {
    printf("provenance msc=%d atl=%d pointer=%zu wchar=%zu entry=%zu lsub=%zu\n",_MSC_FULL_VER,_ATL_VER,sizeof(void*),sizeof(wchar_t),sizeof(STSEntry),sizeof(LSub));
    printf("offsets str=%zu unicode=%zu style=%zu actor=%zu effect=%zu margins=%zu layer=%zu start=%zu end=%zu readorder=%zu\n",
        offsetof(STSEntry,str),offsetof(STSEntry,fUnicode),offsetof(STSEntry,style),offsetof(STSEntry,actor),offsetof(STSEntry,effect),
        offsetof(STSEntry,marginRect),offsetof(STSEntry,layer),offsetof(STSEntry,start),offsetof(STSEntry,end),offsetof(STSEntry,readorder));
    maps(); cstring_matrix(); sorts(); numeric();
    puts("unresolved comparator-overflow OOM invalid-fps out-of-range-llround production-integration");
    puts("document-primitives-v1 complete");
}
