// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_drawing.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

static uint64_t comparisons = 0;
static void check(const std::vector<DrawingToken>& tokens, bool cached=false) {
    OriginalDrawing original(tokens);
    CandidateDrawing candidate(tokens);
    if (cached) {
        original.m_pathTypesOrg.Add(PT_MOVETO); candidate.m_pathTypesOrg.Add(PT_MOVETO);
        original.m_pathPointsOrg.Add(CPoint(7, 9)); candidate.m_pathPointsOrg.Add(CPoint(7, 9));
    }
    const bool a = original.ParseStr(), b = candidate.ParseStr();
    if (a != b || original.m_pathTypesOrg != candidate.m_pathTypesOrg ||
        original.m_pathPointsOrg != candidate.m_pathPointsOrg || original.requests != candidate.requests) {
        fprintf(stderr, "Drawing mismatch at comparison %llu:", static_cast<unsigned long long>(comparisons));
        for (const auto& token : tokens) fprintf(stderr, " %04x/%zu", static_cast<unsigned>(token.command), token.points.size());
        fprintf(stderr, "\n"); exit(1);
    }
    ++comparisons;
}
static DrawingToken token(WCHAR command, int count, int failure=-1) {
    DrawingToken result = {command, {}};
    const int32_t values[] = {0, 1, -1, INT32_MIN, INT32_MAX, 64, -999};
    for (int i=0; i<count; ++i)
        result.points.push_back({i != failure, CPoint(values[i%7], values[(i+3)%7])});
    return result;
}
static void enumerate(std::vector<DrawingToken>& input, int remaining) {
    check(input);
    if (!remaining) return;
    const DrawingToken choices[] = {token('m',0), token('m',1), token('n',1), token('l',2),
        token('b',2), token('s',2), token('s',5), token('s',5,1), token('p',3), token('c',0)};
    for (const auto& choice : choices) { input.push_back(choice); enumerate(input,remaining-1); input.pop_back(); }
}
int main() {
    std::vector<DrawingToken> input;
    enumerate(input, 5);
    // Every raw command code unit, with and without a preceding move. This also
    // proves that unknown commands do not consume any numeric input.
    for (uint32_t c=0; c<=0xffff; ++c) {
        check({token(static_cast<WCHAR>(c), 5)});
        check({token('m',3), token(static_cast<WCHAR>(c),5,1), token('c',0)});
    }
    uint32_t random = 0x7d913a49;
    const WCHAR commands[] = {'m','n','l','b','s','p','c','?',0};
    for (unsigned iteration=0; iteration<100000; ++iteration) {
        input.clear();
        for (unsigned i=0, length=iteration%60; i<length; ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            input.push_back(token(commands[random%9], (random>>8)%9, static_cast<int>((random>>16)%10)-1));
        }
        check(input);
        if (iteration%100==0) check(input, true);
    }
    printf("%llu exact drawing-state comparisons passed (types, points, validity, point requests and cache bypass)\n",
        static_cast<unsigned long long>(comparisons));
}
