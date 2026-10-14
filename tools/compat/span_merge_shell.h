// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <utility>
#include <algorithm>
#include <cerrno>
#include <cfenv>
#include "xy_rust_spans.h"
#define _MAX (std::max)
typedef std::pair<uint64_t,uint64_t> tSpan;
typedef std::vector<tSpan> tSpanBuffer;
struct UnresolvedSpanTail {};
static uint64_t spanHandled=0,spanFallbacks=0;
namespace XyRust {
class ObservedSpanMerge : public SpanMerge {
public:
    template<class Spans> ObservedSpanMerge(const Spans& a,const Spans& b,uint64_t x,uint64_t y):SpanMerge(a,b,x,y){}
    bool Ready()const{bool ok=SpanMerge::Ready();if(ok)++spanHandled;else ++spanFallbacks;return ok;}
};
}
