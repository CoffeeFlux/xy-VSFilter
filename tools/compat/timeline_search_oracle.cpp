// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_timeline_search.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

static uint64_t comparisons=0, injected=0;
struct Outcome {
    int returned=123, index=456, count=789, exception=0;
    std::vector<std::vector<int> > requests;
    bool operator==(const Outcome& other) const {
        return returned==other.returned && index==other.index && count==other.count &&
            exception==other.exception && requests==other.requests;
    }
};
template<typename Search> static Outcome run(const std::vector<STSEntry>& entries,
    int method, int64_t time, int mode, double fps, int outputs, int failAt) {
    Search search;
    Outcome out;
    search.m_mode=mode; search.failAt=failAt;
    for (const auto& entry: entries) {
        search.m_entries.Add(entry); search.m_segments.Add(STSSegment(entry.start,entry.end));
    }
    int* index=outputs&1 ? &out.index : nullptr;
    int* count=outputs&2 ? &out.count : nullptr;
    if (outputs==4) index=count=&out.index; // legal aliasing of public outputs
    search.observedIndex=index; search.observedCount=count;
    try {
        if (method==0) out.returned=search.SearchSub(time,fps);
        else {
            const STSSegment* segment=method==1 ? search.SearchSubs(time,fps,index,count) : search.SearchSubs2(time,fps,index,count);
            out.returned=-1;
            if (segment) {
                out.returned=-2;
                for (size_t i=0; i<search.m_segments.GetCount(); ++i)
                    if (segment==&search.m_segments[i]) out.returned=static_cast<int>(i);
            }
        }
    } catch (int error) { out.exception=error; }
    out.requests=search.requests;
    return out;
}
static void check(const std::vector<STSEntry>& entries, int64_t time,
    int mode=TIME, double fps=25.0, int outputs=3, bool exceptions=false) {
    for (int method=0; method<3; ++method) {
        const Outcome expected=run<OriginalTimelineSearch>(entries,method,time,mode,fps,outputs,-1);
        const int count=exceptions ? static_cast<int>(expected.requests.size()) : 0;
        for (int failure=0; failure<=count; ++failure) {
            const int failAt=failure ? failure : -1;
            const Outcome a=failure ? run<OriginalTimelineSearch>(entries,method,time,mode,fps,outputs,failAt) : expected;
            const Outcome b=run<CandidateTimelineSearch>(entries,method,time,mode,fps,outputs,failAt);
            if (!(a==b)) {
                fprintf(stderr,"Timeline lookup mismatch comparison %llu method %d time %lld mode %d fps %.17g outputs %d failure %d\n",
                    static_cast<unsigned long long>(comparisons),method,static_cast<long long>(time),mode,fps,outputs,failAt);
                for (const auto& entry:entries) fprintf(stderr," [%lld,%lld]",static_cast<long long>(entry.start),static_cast<long long>(entry.end));
                fprintf(stderr,"\nexpected %d/%d/%d/exception%d, actual %d/%d/%d/exception%d\n",
                    a.returned,a.index,a.count,a.exception,b.returned,b.index,b.count,b.exception);
                exit(1);
            }
            ++comparisons;
            if (failure) ++injected;
        }
    }
}
static void enumerate(std::vector<STSEntry>& input,int remaining) {
    for (int64_t time=-2; time<=2; ++time) check(input,time);
    if (!remaining) return;
    for (int64_t start=-1; start<=1; ++start) for (int64_t end=-1; end<=1; ++end) {
        input.push_back({start,end}); enumerate(input,remaining-1); input.pop_back();
    }
}
int main() {
    std::vector<STSEntry> input;
    enumerate(input,5); // intentionally includes unsorted and reversed timelines
    const int64_t edge[]={INT64_MIN,INT64_MIN+1,-1,0,1,INT64_MAX-1,INT64_MAX};
    for (auto start:edge) for (auto end:edge) for (auto time:edge)
        for (int outputs=0;outputs<=4;++outputs) check({{start,end},{0,1}},time,TIME,25.0,outputs,true);
    uint32_t random=0x194582ce;
    for (unsigned trial=0;trial<12000;++trial) {
        input.clear();
        for (unsigned i=0;i<trial%50;++i) {
            random ^= random<<13; random ^= random>>17; random ^= random<<5;
            const int64_t a=static_cast<int64_t>(random%2001)-1000;
            random ^= random<<13; random ^= random>>17; random ^= random<<5;
            const int64_t b=static_cast<int64_t>(random%2001)-1000;
            input.push_back({a,b});
        }
        if (trial&1) std::sort(input.begin(),input.end(),[](const STSEntry& a,const STSEntry& b){return a.start<b.start;});
        const double rates[]={23.976,25.0,29.97,60.0,-25.0,10000000.0};
        const int64_t time=static_cast<int64_t>(random%1000000001)-500000000;
        check(input,time,trial%3,rates[trial%6],trial%5,trial%20==0);
    }
    printf("%llu exact timeline lookup comparisons passed (%llu injected translation exceptions; returned pointer/index, aliased outputs and exact native translation call order)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(injected));
}
