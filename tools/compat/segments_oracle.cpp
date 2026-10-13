// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_segments.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

static uint64_t comparisons=0;
static void check(const std::vector<STSEntry>& entries) {
    OriginalSegments original;
    CandidateSegments candidate;
    for (const auto& entry: entries) {
        original.m_entries.Add(entry); candidate.m_entries.Add(entry);
    }
    // Old cached state must be discarded, including its animated flag.
    STSSegment old(-99, 99); old.animated=true; old.subs.Add(123);
    original.m_segments.Add(old); candidate.m_segments.Add(old);
    for (int pass=0; pass<2; ++pass) {
        original.CreateSegments(); candidate.CreateSegments();
        if (SegmentSnapshot(original.m_segments) != SegmentSnapshot(candidate.m_segments) ||
            original.callbacks != candidate.callbacks) {
            fprintf(stderr, "Segment mismatch at comparison %llu, pass %d:",
                static_cast<unsigned long long>(comparisons), pass);
            for (const auto& entry: entries) fprintf(stderr, " [%lld,%lld]",
                static_cast<long long>(entry.start), static_cast<long long>(entry.end));
            fprintf(stderr, "\n"); exit(1);
        }
        ++comparisons;
        // Exercise replacement of a previously populated timeline by an empty
        // one as well as the initial construction above.
        original.m_entries.RemoveAll(); candidate.m_entries.RemoveAll();
    }
}
static void enumerate(std::vector<STSEntry>& input, int remaining) {
    check(input);
    if (!remaining) return;
    for (int64_t start=-1; start<=1; ++start) {
        for (int64_t end=-1; end<=1; ++end) {
            input.push_back({start,end}); enumerate(input,remaining-1); input.pop_back();
        }
    }
}
int main() {
    std::vector<STSEntry> input;
    enumerate(input,5);
    const int64_t edge[] = {INT64_MIN, INT64_MIN+1, -10000000, -1, 0, 1, 10000000, INT64_MAX-1, INT64_MAX};
    for (auto a: edge) for (auto b: edge) for (auto c: edge) for (auto d: edge) check({{a,b},{c,d}});
    uint32_t random=0x754932a1;
    for (unsigned trial=0; trial<30000; ++trial) {
        input.clear();
        for (unsigned i=0; i<trial%64; ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            const int64_t a=static_cast<int64_t>(random%129)-64;
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            const int64_t b=static_cast<int64_t>(random%129)-64;
            input.push_back({a,b});
        }
        check(input);
    }
    printf("%llu exact segment comparisons passed (boundaries, input entry order, inactive-entry splits, animated reset and callback snapshots)\n",
        static_cast<unsigned long long>(comparisons));
}
