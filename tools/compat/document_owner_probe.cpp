// SPDX-License-Identifier: GPL-2.0-or-later
// Actual ATL values around the experimental Rust storage owner. Pinned native
// method bodies are generated separately; no production host/thread parity claim.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atlbase.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atltypes.h>
#include <dshow.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>
#include <stdexcept>
#define TRACE_SUB(...) ((void)0)
#define UNITS_FLOAT (10000000.0)
#define EXTVTT 1
#ifndef _I64_MAX
#define _I64_MAX INT64_MAX
#endif
using std::min;
using std::max;
#include "STS.h"
#include "RustDocumentOwner.h"
#include "document_owner_reference.inc"

static unsigned checks=0,states=0;
static void check(bool value,const char* detail) {
    if(!value)throw std::runtime_error(detail);
    ++checks;
}
template<class String> static bool same_units(const String& a,const String& b) {
    if(a.GetLength()!=b.GetLength())return false;
    for(int i=0;i<a.GetLength();++i)if(a[i]!=b[i])return false;
    return true;
}
static bool same_entry(const STSEntry& a,const STSEntry& b) {
    return same_units(a.str,b.str)&&same_units(a.style,b.style)&&same_units(a.actor,b.actor)&&same_units(a.effect,b.effect)
        &&a.fUnicode==b.fUnicode&&a.marginRect==b.marginRect&&a.layer==b.layer&&a.start==b.start&&a.end==b.end&&a.readorder==b.readorder;
}
static bool same_segment(const STSSegment& a,const STSSegment& b) {
    if(a.start!=b.start||a.end!=b.end||a.animated!=b.animated||a.subs.GetCount()!=b.subs.GetCount())return false;
    for(size_t i=0;i<a.subs.GetCount();++i)if(a.subs[i]!=b.subs[i])return false;
    return true;
}
template<class String> static void dump_units(const String& value) {
    printf(" %d",value.GetLength());for(int i=0;i<value.GetLength();++i)printf(" %04x",unsigned(value[i]));
}
static void compare(PinnedOwner& native,XyRust::DocumentOwner& rust,const char* stage) {
    ++states;check(rust.Count()==native.m_entries.GetCount(),"entry count");check(rust.SegmentCount()==native.m_segments.GetCount(),"segment count");
    printf("state %u %s %zu %zu\n",states,stage,rust.Count(),rust.SegmentCount());
    for(size_t i=0;i<native.m_entries.GetCount();++i){
        const STSEntry& expected=native.m_entries[i];STSEntry actual=rust.Entry(static_cast<int>(i));
        check(same_entry(actual,expected),"complete initialized entry");
        check(rust.EntryStart(int(i))==expected.start&&rust.EntryEnd(int(i))==expected.end,"endpoint scalar");
        check(rust.EntryLayer(int(i))==expected.layer&&rust.EntryReadOrder(int(i))==expected.readorder,"ordering scalar");
        check(same_units(rust.EntryEffect(int(i)),expected.effect),"effect scalar raw units");
        printf("entry %zu",i);dump_units(actual.str);dump_units(actual.style);dump_units(actual.actor);dump_units(actual.effect);
        printf(" %d %ld %ld %ld %ld %d %lld %lld %d\n",actual.fUnicode,actual.marginRect.left,actual.marginRect.top,actual.marginRect.right,actual.marginRect.bottom,actual.layer,actual.start,actual.end,actual.readorder);
    }
    for(size_t i=0;i<native.m_segments.GetCount();++i){
        STSSegment actual(-91,91);check(rust.Segment(int(i),actual),"segment found");
        const STSSegment& expected=native.m_segments[i];check(same_segment(actual,expected),"complete initialized segment");
        size_t members=999;check(rust.SegmentEntryCount(int(i),members)&&members==expected.subs.GetCount(),"member count scalar");
        check(rust.SegmentStart(int(i))==expected.start&&rust.SegmentEnd(int(i))==expected.end,"segment endpoint scalar");
        check(rust.IsAnimated(unsigned(i))==expected.animated,"animation scalar");
        printf("segment %zu %lld %lld %d %zu",i,actual.start,actual.end,actual.animated,actual.subs.GetCount());
        for(size_t j=0;j<actual.subs.GetCount();++j)printf(" %d",actual.subs[j]);puts("");
    }
    for(REFERENCE_TIME time:{-1LL,0LL,1LL,2LL,4LL,5LL,10LL,20LL,5LL,0LL})for(unsigned mode=0;mode<3;++mode){
        int index=-1,count=0;bool found=false;
        if(mode==0){index=native.SearchSub(time,25.0);count=int(native.m_entries.GetCount());found=index>=0;}
        else found=(mode==1?native.SearchSubs(time,25.0,&index,&count):native.SearchSubs2(time,25.0,&index,&count))!=NULL;
        const auto actual=rust.Query(mode,time);
        check(actual.index==index&&actual.found==found&&actual.count==size_t(count),"native TIME query");
        printf("query %u %lld %d %d %d\n",mode,time,index,count,found);
        if(mode){
            ExperimentalFacade facade(rust);int direct_shared=771,shared=772;
            const STSSegment* direct=mode==1?native.SearchSubs(time,25.0,&direct_shared,&direct_shared):native.SearchSubs2(time,25.0,&direct_shared,&direct_shared);
            STSSegment snapshot(71,81);snapshot.animated=true;snapshot.subs.Add(917);
            const bool actual_alias=mode==1?facade.FindNextSegment(time,25.0,&shared,&shared):facade.SearchSubs2Snapshot(time,25.0,snapshot,&shared,&shared);
            check(actual_alias==(direct!=NULL)&&shared==direct_shared,"experimental facade aliased outputs");
            if(mode==2){if(direct)check(same_segment(snapshot,*direct),"aliased snapshot value");else check(snapshot.start==71&&snapshot.end==81&&snapshot.animated&&snapshot.subs.GetCount()==1&&snapshot.subs[0]==917,"aliased missed snapshot untouched");}
            printf("facade-alias %u %lld %d %d\n",mode,time,shared,actual_alias);
        }

    }
    // FRAME translation remains the actual native CRT expression on both sides;
    // this checks the facade's handoffs/output order, not a Rust rounding port.
    native.m_mode=PinnedOwner::FRAME;
    ExperimentalFacade facade(rust);facade.m_mode=ExperimentalFacade::FRAME;
    for(REFERENCE_TIME time:{-1LL,0LL,400000LL,800000LL,1600000LL,2000000LL,4000000LL,8000000LL})for(unsigned mode=1;mode<3;++mode){
        int direct_shared=771,shared=772;
        const STSSegment* direct=mode==1?native.SearchSubs(time,25.0,&direct_shared,&direct_shared):native.SearchSubs2(time,25.0,&direct_shared,&direct_shared);
        STSSegment snapshot(71,81);snapshot.animated=true;snapshot.subs.Add(917);
        const bool found=mode==1?facade.FindNextSegment(time,25.0,&shared,&shared):facade.SearchSubs2Snapshot(time,25.0,snapshot,&shared,&shared);
        check(found==(direct!=NULL)&&shared==direct_shared,"FRAME facade aliased outputs");
        if(mode==2){if(direct)check(same_segment(snapshot,*direct),"FRAME alias snapshot");else check(snapshot.start==71&&snapshot.end==81&&snapshot.animated&&snapshot.subs.GetCount()==1&&snapshot.subs[0]==917,"FRAME alias miss untouched");}
        printf("facade-frame-alias %u %lld %d %d\n",mode,time,shared,found);
    }
    native.m_mode=PinnedOwner::TIME;
}
static STSEntry entry(int id,REFERENCE_TIME start,REFERENCE_TIME end,int order=-1) {
    STSEntry value;
    const wchar_t raw[]={wchar_t(id),0,0xd800,0xdcff,0xffff};
    value.str=CStringW(raw,5);value.fUnicode=id%2!=0;value.style=L"Default";
    value.actor=CString(raw,5);value.effect=CString(raw+1,4);
    value.marginRect=CRect(id,-id,id+1,-id-1);value.layer=id%3;value.start=start;value.end=end;value.readorder=order;
    return value;
}
static void add(PinnedOwner& native,XyRust::DocumentOwner& rust,const STSEntry& value,unsigned mode,const char* stage) {
    native.AddPrepared(value,mode);rust.AddPrepared(value,mode);compare(native,rust,stage);
}
static void sort(PinnedOwner& native,XyRust::DocumentOwner& rust,bool restore) {
    CAtlArray<STSEntry> before;before.Copy(native.m_entries);native.Sort(restore);
    std::vector<uint32_t> order;
    for(size_t i=0;i<native.m_entries.GetCount();++i){
        size_t original=0;while(original<before.GetCount()&&before[original].marginRect.left!=native.m_entries[i].marginRect.left)++original;
        check(original<before.GetCount(),"sort identity");order.push_back(uint32_t(original));
    }
    rust.Reorder(order);compare(native,rust,restore?"sort-readorder":"sort-normal");
}
static void lifecycle() {
    PinnedOwner native;XyRust::DocumentOwner rust;
    compare(native,rust,"empty");
    add(native,rust,entry(1,0,10,9),0,"add-first");
    native.AnimationOr(0,true);rust.AnimationOr(0,true);compare(native,rust,"animation-true");
    add(native,rust,entry(2,2,8,0),0,"add-overlap");
    add(native,rust,entry(3,4,4),0,"add-zero-rejected");
    add(native,rust,entry(4,4,4),1,"entry-only-zero");
    add(native,rust,entry(5,5,15,1),1,"entry-only-overlap");
    native.CreateSegments();rust.Rebuild();compare(native,rust,"rebuild");
    native.AnimationOr(0,true);rust.AnimationOr(0,true);native.AnimationOr(0,false);rust.AnimationOr(0,false);compare(native,rust,"animation-or");
    const wchar_t prepared_text[]={0xdc00,0,L'X',0xd800};
    CStringW text(prepared_text,4);native.SetText(0,text,-1);rust.SetText(0,text);compare(native,rust,"prepared-text-retain-flag");
    native.SetText(1,text,1);rust.SetText(1,text,1);compare(native,rust,"prepared-text-set-flag");
    native.SetText(2,CStringW(),-1);rust.SetText(2,CStringW());compare(native,rust,"prepared-text-empty");
    const wchar_t a[]={L'A',0,L'B'},old[]={L'A',0,L'C'},replacement[]={0xdcff,0,0xd800,L'X'};
    native.m_entries[0].style=CString(a,3);rust.SetStyle(0,CString(a,3));compare(native,rust,"prepared-style");
    native.RenameStyle(CString(old,3),CString(replacement,4));rust.RenameStyle(CString(old,3),CString(replacement,4));compare(native,rust,"cstring-nul-rename");
    STSEntry isolated=rust.Entry(0);isolated.str.SetAt(0,L'Q');isolated.style.SetAt(3,L'Y');compare(native,rust,"entry-copy-independence");
    STSEntry negative=native.m_entries[0];negative.style.SetAt(3,L'Z');check(!same_entry(negative,native.m_entries[0]),"negative NUL-suffix comparator control");
    PinnedOwner native_copy;native_copy.CopyStorage(native);XyRust::DocumentOwner copy;copy.CopyFrom(rust);compare(native_copy,copy,"storage-copy");
    native_copy.SetText(0,CStringW(L"copy"),0);copy.SetText(0,CStringW(L"copy"),0);compare(native_copy,copy,"copy-mutation");compare(native,rust,"source-copy-independence");
    native.m_entries[0].end=7;rust.SetTime(0,false,7);compare(native,rust,"endpoint-with-stale-segments");
    native.CorrectLastEnd(6);rust.CorrectLastEnd(6);compare(native,rust,"last-stored-correction");
    native.CorrectLastEnd(4);rust.CorrectLastEnd(4);compare(native,rust,"last-end-before-start");
    sort(native,rust,false);sort(native,rust,true);
    std::vector<std::pair<int64_t,int64_t>> times;
    for(size_t i=0;i<native.m_entries.GetCount();++i)times.push_back({int64_t(i*2),int64_t(i*2+3)});
    native.ReplaceTimes(times);rust.ReplaceTimes(times);compare(native,rust,"prepared-time-rebuild");
    native.RemoveAllEntries();rust.RemoveEntries();compare(native,rust,"remove-retains-stale-segments");
    native.CreateSegments();rust.Rebuild();compare(native,rust,"rebuild-clears-stale-segments");
    add(native,rust,entry(8,0,1),0,"add-after-rebuild");
    native.ClearStorage();rust.Clear();compare(native,rust,"clear-storage");
    add(native,rust,entry(9,5,10),2,"vtt-first");
    add(native,rust,entry(10,5,10),2,"vtt-duplicate");
    add(native,rust,entry(11,3,5),2,"vtt-live-lower-bound");
    native.ClearStorage();rust.Clear();compare(native,rust,"clear-after-vtt");
    bool refused=false;try{rust.Entry(0);}catch(const XyRust::DocumentStatusError& error){refused=error.Status()==XY_DOCUMENT_V1_OUT_OF_RANGE;}
    check(refused,"bounded invalid entry status");
}
static void snapshot_lifetime() {
    std::unique_ptr<XyRust::DocumentReply> saved_entry,saved_segment;
    std::vector<uint8_t> entry_bytes,segment_bytes;
    {
        XyRust::DocumentOwner owner;owner.AddPrepared(entry(21,0,10),0);
        saved_entry.reset(new XyRust::DocumentReply(owner.SnapshotEntry(0)));
        saved_segment.reset(new XyRust::DocumentReply(owner.SnapshotSegment(0)));
        entry_bytes=saved_entry->Read();segment_bytes=saved_segment->Read();
        check(saved_entry->Size()==entry_bytes.size()&&saved_segment->Size()==segment_bytes.size(),"snapshot size");
        owner.Clear();check(saved_entry->Read()==entry_bytes&&saved_segment->Read()==segment_bytes,"snapshot survives clear");
    }
    check(saved_entry->Read()==entry_bytes&&saved_segment->Read()==segment_bytes,"snapshot survives owner destruction");
    saved_entry->Close();bool refused=false;
    try{saved_entry->Size();}catch(const XyRust::DocumentStatusError& error){refused=error.Status()==XY_DOCUMENT_V1_BAD_HANDLE;}
    check(refused,"closed snapshot checked status");
    puts("owner-snapshot-isolation complete");
}
int main() {
    try {
        printf("provenance msc=%d atl=%d pointer=%zu wchar=%zu entry=%zu\n",_MSC_FULL_VER,_ATL_VER,sizeof(void*),sizeof(wchar_t),sizeof(STSEntry));
        lifecycle();snapshot_lifetime();
        printf("checks %u states %u negative-control=passed\n",checks,states);
        puts("document-owner-v1 complete");return 0;
    }catch(const std::exception& error){fprintf(stderr,"owner probe: %s\n",error.what());return 2;}
}
