// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "xy_rust_be_blur.h"
typedef uint16_t WORD;
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#define BLUR_NATIVE_SSE 1
#include <emmintrin.h>
#else
#define BLUR_NATIVE_SSE 0
#endif
struct BlurAllocation { void* pointer; size_t size; };
static std::vector<BlurAllocation> blurAllocations;
static std::vector<uint64_t> blurEvents;
static std::vector<WORD> blurHistory;
static int blurFail=-1;
static void* xy_malloc(size_t size) {
    const size_t index=blurAllocations.size();
    blurEvents.push_back(0);blurEvents.push_back(size);
    void* pointer=nullptr;
    if(static_cast<int>(index)!=blurFail && size<=65536) {
        pointer=std::malloc(size ? size : 1);
        if(!pointer)std::abort();
    }
    blurAllocations.push_back({pointer,size});blurEvents.push_back(pointer ? 1 : 0);
    return pointer;
}
static void xy_free(void* pointer) {
    for(size_t i=0;i<blurAllocations.size();++i)if(blurAllocations[i].pointer==pointer && pointer) {
        blurEvents.push_back(1);blurEvents.push_back(i);
        const WORD* words=static_cast<const WORD*>(pointer);
        blurHistory.insert(blurHistory.end(),words,words+blurAllocations[i].size/sizeof(WORD));
        std::free(pointer);blurAllocations[i].pointer=nullptr;return;
    }
    std::abort();
}
