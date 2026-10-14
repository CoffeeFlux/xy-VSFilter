// SPDX-License-Identifier: GPL-2.0-or-later
// Actual ATL native-backed facade isolation checks; not renderer/Rust parity.
#include <windows.h>
#include <atlbase.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atltypes.h>
#include <dshow.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <initializer_list>
#define TRACE_SUB(...) ((void)0)
#include "document_facade_extracted.inc"

static int checks=0;
static void check(bool condition, const char* name) {
    if(!condition) { fprintf(stderr,"failed: %s\n",name); std::exit(2); }
    ++checks;
}
static void initialize(CSimpleTextSubtitle& doc) {
    STSEntry e;
    const wchar_t raw[]={L'A',0,0xd800,L'Z'};
    e.str=CStringW(raw,4);e.fUnicode=false;e.style=L"Default";e.actor=L"actor";e.effect=L"Banner;7;1";
    e.marginRect=CRect(-1,2,3,4);e.layer=-3;e.start=0;e.end=2;e.readorder=9;
    doc.m_entries.Add(e);
    e.start=4;e.end=6;e.layer=7;e.readorder=1;doc.m_entries.Add(e);
    STSSegment first(0,2);first.subs.Add(0);doc.m_segments.Add(first);
    STSSegment second(4,6);second.subs.Add(1);doc.m_segments.Add(second);
}
static void compare_entries(CSimpleTextSubtitle& doc) {
    check(doc.GetEntryCount()==doc.m_entries.GetCount(),"entry-count");
    for(int i=0;i<static_cast<int>(doc.m_entries.GetCount());++i) {
        const STSEntry& original=doc.m_entries[i];
        STSEntry copy=doc.GetEntrySnapshot(i);
        check(copy.str.GetLength()==original.str.GetLength(),"text-length");
        for(int j=0;j<original.str.GetLength();++j) check(copy.str[j]==original.str[j],"text-code-unit");
        check(copy.style==original.style && copy.actor==original.actor && copy.effect==original.effect,"string-fields");
        check(copy.fUnicode==original.fUnicode && copy.marginRect==original.marginRect,"flag-margins");
        check(copy.start==original.start && copy.end==original.end && copy.layer==original.layer && copy.readorder==original.readorder,"scalar-fields");
        check(doc.GetEntryEnd(i)==original.end && doc.GetEntryLayer(i)==original.layer && doc.GetEntryReadOrder(i)==original.readorder,"scalar-access");
        check(doc.GetEntryEffect(i)==original.effect,"effect-value");
        printf("entry %d text-length=%d start=%lld end=%lld layer=%d order=%d\n",i,copy.str.GetLength(),copy.start,copy.end,copy.layer,copy.readorder);
    }
}
static int pointer_next(CSimpleTextSubtitle& doc,int index) {
    const STSSegment* segment=index>=0 && index<static_cast<int>(doc.m_segments.GetCount())?&doc.m_segments[index]:NULL;
    while(segment && segment->subs.GetCount()==0) {
        ++index;segment=index>=0 && index<static_cast<int>(doc.m_segments.GetCount())?&doc.m_segments[index]:NULL;
    }
    return segment?index+1:0;
}
static int scalar_next(CSimpleTextSubtitle& doc,int index) {
    size_t count=999;bool found=doc.GetSegmentEntryCount(index,count);
    while(found && count==0) {++index;found=doc.GetSegmentEntryCount(index,count);}
    return found?index+1:0;
}
static void compare_queries(CSimpleTextSubtitle& doc) {
    PinnedReference reference; reference.m_segments.Copy(doc.m_segments);
    for(REFERENCE_TIME t : {-2LL,-1LL,0LL,1LL,2LL,3LL,4LL,5LL,6LL,7LL}) {
        int direct_index=999,direct_count=999,index=998,count=998;
        const STSSegment* direct=reference.SearchSubs(t,24.0,&direct_index,&direct_count);
        const bool found=doc.FindNextSegment(t,24.0,&index,&count);
        check(found==(direct!=NULL) && index==direct_index && count==direct_count,"next-search");
        printf("next %lld %d %d %d\n",t,index,count,found);
        direct_index=999;direct_count=999;index=998;count=998;
        direct=reference.SearchSubs2(t,24.0,&direct_index,&direct_count);
        STSSegment snapshot(71,81);snapshot.animated=true;snapshot.subs.Add(917);
        const bool containing=doc.SearchSubs2Snapshot(t,24.0,snapshot,&index,&count);
        check(containing==(direct!=NULL) && index==direct_index && count==direct_count,"containing-search");
        if(direct) {
            check(snapshot.start==direct->start && snapshot.end==direct->end && snapshot.animated==direct->animated,"segment-scalars");
            check(snapshot.subs.GetCount()==direct->subs.GetCount(),"member-count");
            for(size_t j=0;j<direct->subs.GetCount();++j)check(snapshot.subs[j]==direct->subs[j],"member-value");
        } else {
            check(snapshot.start==71 && snapshot.end==81 && snapshot.animated && snapshot.subs.GetCount()==1 && snapshot.subs[0]==917,"absent-output-untouched");
        }
        printf("containing %lld %d %d %d\n",t,index,count,containing);
        int direct_shared=771,shared=772;
        direct=reference.SearchSubs(t,24.0,&direct_shared,&direct_shared);
        const bool alias_next=doc.FindNextSegment(t,24.0,&shared,&shared);
        check(alias_next==(direct!=NULL)&&shared==direct_shared,"next-aliased-output-write-order");
        printf("next-alias %lld %d %d\n",t,shared,alias_next);
        direct_shared=771;shared=772;
        direct=reference.SearchSubs2(t,24.0,&direct_shared,&direct_shared);
        STSSegment alias_snapshot(71,81);alias_snapshot.animated=true;alias_snapshot.subs.Add(917);
        const bool alias_containing=doc.SearchSubs2Snapshot(t,24.0,alias_snapshot,&shared,&shared);
        check(alias_containing==(direct!=NULL)&&shared==direct_shared,"containing-aliased-output-write-order");
        if(!direct)check(alias_snapshot.start==71&&alias_snapshot.end==81&&alias_snapshot.animated&&alias_snapshot.subs.GetCount()==1&&alias_snapshot.subs[0]==917,"aliased-miss-snapshot-untouched");
        printf("containing-alias %lld %d %d\n",t,shared,alias_containing);

    }
    for(int i=-1;i<=static_cast<int>(doc.m_segments.GetCount());++i) {
        size_t count=987;
        bool found=doc.GetSegmentEntryCount(i,count);
        const bool direct=i>=0 && i<static_cast<int>(doc.m_segments.GetCount());
        check(found==direct,"segment-count-bounds");
        if(direct)check(count==doc.m_segments[i].subs.GetCount(),"segment-count-value");
        else check(count==987,"absent-count-untouched");
        check(pointer_next(doc,i)==scalar_next(doc,i),"schedule-next");
    }
}
static void check_effect_argument(const CStringW& effect) {
    check(effect.GetLength()==10 && effect[0]==L'B' && effect[9]==L'1',"effect-temporary-full-call-lifetime");
}
static void isolation(CSimpleTextSubtitle& doc) {
    check_effect_argument(doc.GetEntryEffect(0));
    STSEntry entry=doc.GetEntrySnapshot(0);
    entry.str.SetAt(0,L'B');entry.style=L"Other";entry.actor.SetAt(0,L'X');entry.effect.SetAt(0,L'X');entry.marginRect.left=88;
    check(doc.m_entries[0].str[0]==L'A' && doc.m_entries[0].style==L"Default" && doc.m_entries[0].actor[0]==L'a' && doc.m_entries[0].effect[0]==L'B' && doc.m_entries[0].marginRect.left==-1,"entry-snapshot-independent");
    CString effect=doc.GetEntryEffect(0);effect.SetAt(0,L'X');
    check(doc.m_entries[0].effect[0]==L'B',"effect-snapshot-independent");
    STSSegment snapshot;
    check(doc.SearchSubs2Snapshot(0,24.0,snapshot,NULL,NULL),"snapshot-without-output-pointers");
    snapshot.subs[0]=99;snapshot.subs.Add(100);snapshot.start=-10;snapshot.animated=true;
    check(doc.m_segments[0].subs.GetCount()==1 && doc.m_segments[0].subs[0]==0 && doc.m_segments[0].start==0 && !doc.m_segments[0].animated,"segment-snapshot-independent");
    doc.OrSegmentAnimated(0,true);doc.OrSegmentAnimated(0,false);
    check(doc.IsSegmentAnimated(0) && !doc.IsSegmentAnimated(1) && !doc.IsSegmentAnimated(static_cast<unsigned>(-1)) && !doc.IsSegmentAnimated(2),"animation-or-bounds");
    STSSegment retained;
    check(doc.SearchSubs2Snapshot(0,24.0,retained,NULL,NULL),"retained-snapshot");
    doc.m_segments[0].subs.Add(1);doc.m_segments.RemoveAll();
    check(retained.subs.GetCount()==1 && retained.subs[0]==0 && retained.animated,"snapshot-survives-document-mutation");
    puts("isolation complete");
}
int main() {
    printf("provenance msc=%d atl=%d pointer=%zu entry=%zu segment=%zu\n",_MSC_FULL_VER,_ATL_VER,sizeof(void*),sizeof(STSEntry),sizeof(STSSegment));
    CSimpleTextSubtitle empty;compare_entries(empty);compare_queries(empty);
    CSimpleTextSubtitle doc;initialize(doc);compare_entries(doc);compare_queries(doc);
    STSSegment gap(2,4);doc.m_segments.InsertAt(1,gap);compare_queries(doc);
    // Stale member indices are copied, never dereferenced by segment queries.
    doc.m_entries.RemoveAll();compare_queries(doc);
    doc.m_segments.RemoveAll();initialize(doc);isolation(doc);
    printf("checks %d\n",checks);
    puts("document-facade-v1 complete");
}
