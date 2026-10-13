// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_segment_insert.h"
#include <cstdio>
#include <cstdlib>

struct Event { int64_t start,end; int order; };
struct Outcome {
    std::vector<int64_t> state;
    std::vector<std::vector<int64_t> > trace;
    int exception=0;
    bool operator==(const Outcome& other) const { return state==other.state && trace==other.trace && exception==other.exception; }
};
template<typename Owner> static std::vector<int64_t> snapshot(const Owner& owner) {
    auto state=SegmentSnapshot(owner.m_segments);
    state.push_back(static_cast<int64_t>(owner.m_entries.GetCount()));
    for (size_t i=0;i<owner.m_entries.GetCount();++i) {
        const auto& e=owner.m_entries[i]; state.push_back(e.start); state.push_back(e.end); state.push_back(e.readorder);
    }
    return state;
}
template<typename Owner> static Outcome run(const std::vector<Event>& events,int failure,int seed=0,bool vtt=false) {
    Owner owner;
    owner.m_subtitleType=vtt ? EXTVTT : 0;
    Outcome result;
    if (seed) {
        owner.m_entries.Add({0,10,9}); owner.m_entries.Add({0,10,1});
        InsertionSegment segment(0,10); segment.subs.Add(0); segment.subs.Add(1); segment.animated=true;
        owner.m_segments.Add(segment);
    }
    insertionObserver=[&](int kind,int op,size_t position) {
        auto state=snapshot(owner);
        state.insert(state.begin(),{kind,op,static_cast<int64_t>(position)});
        result.trace.push_back(state);
        if (static_cast<int>(result.trace.size())==failure) throw 17;
    };
    try {
        for (size_t i=0;i<events.size();++i) {
            // Renderer code can set these between streaming additions.
            for (size_t j=0;j<owner.m_segments.GetCount();++j) owner.m_segments[j].animated=((i+j)%2)==0;
            const auto& e=events[i]; owner.AddPrepared(e.start,e.end,e.order);
        }
    } catch (int error) { result.exception=error; }
    insertionObserver=nullptr;
    result.state=snapshot(owner);
    return result;
}
static uint64_t comparisons=0,injected=0,mutations=0;
static void check(const std::vector<Event>& events,bool exceptions=false,int seed=0,bool vtt=false) {
    const Outcome normal=run<OriginalInsertion>(events,-1,seed,vtt);
    for (int failure=0,last=exceptions ? static_cast<int>(normal.trace.size()) : 0; failure<=last;++failure) {
        const Outcome a=failure ? run<OriginalInsertion>(events,failure,seed,vtt) : normal;
        const Outcome b=run<CandidateInsertion>(events,failure ? failure : -1,seed,vtt);
        if (!(a==b)) {
            fprintf(stderr,"Incremental segment mismatch comparison %llu failure %d seed %d vtt %d\n",
                static_cast<unsigned long long>(comparisons),failure,seed,vtt);
            for (const auto& e:events) fprintf(stderr," [%lld,%lld,%d]",static_cast<long long>(e.start),static_cast<long long>(e.end),e.order);
            fprintf(stderr,"\ntrace lengths %zu/%zu exceptions %d/%d\n",a.trace.size(),b.trace.size(),a.exception,b.exception); exit(1);
        }
        ++comparisons; mutations+=a.trace.size(); if(failure)++injected;
    }
}
static void enumerate(std::vector<Event>& input,int remaining) {
    check(input);
    if(!remaining)return;
    const Event options[]={{0,1,-1},{0,2,1},{1,3,0},{2,3,1},{0,3,0},{1,2,9},{1,1,0},{2,1,0}};
    for(const auto& e:options){input.push_back(e);enumerate(input,remaining-1);input.pop_back();}
}
int main() {
    std::vector<Event> events;
    enumerate(events,5);
    for(int order: {INT32_MIN,-1,0,1,2,9,INT32_MAX}) check({{0,10,order},{2,8,order}},true,1);
    const int64_t edge[]={INT64_MIN,INT64_MIN+1,-1,0,1,INT64_MAX-1,INT64_MAX};
    for(auto a:edge)for(auto b:edge)for(auto c:edge)for(auto d:edge)check({{a,b,-1},{c,d,0}});
    uint32_t random=0x95318b2d;
    for(unsigned trial=0;trial<16000;++trial){
        events.clear();
        for(unsigned i=0;i<trial%24;++i){
            random^=random<<13;random^=random>>17;random^=random<<5;
            const int64_t start=static_cast<int64_t>(random%41)-20;
            events.push_back({start,start+1+(random>>8)%20,static_cast<int>((random>>16)%12)-1});
        }
        check(events,trial%200==0);
    }
    // Differential WebVTT cases here use live lower_bound results. End-of-list
    // insertion has a separate candidate check below, outside these counts.
    check({{5,10,-1},{5,10,-1},{3,5,-1},{3,10,-1},{1,2,-1}},true,0,true);
    // End-of-list insertion is tested against explicit candidate invariants,
    // separately from differential counts: this reference state is unresolved.
    const Outcome vtt=run<CandidateInsertion>({{0,1,-1},{2,3,-1}},-1,0,true);
    const Outcome ordinary=run<CandidateInsertion>({{0,1,-1},{2,3,-1}},-1);
    if (!(vtt==ordinary)) { fprintf(stderr,"Candidate end-of-list insertion failed\n"); return 1; }
    printf("%llu exact incremental-segment comparisons passed (%llu native mutation snapshots, %llu injected boundary exceptions; entry order, animation flags and partial state)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(mutations),static_cast<unsigned long long>(injected));
}
