// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "xy_rust_bilinear.h"
typedef uint16_t WORD;
static bool bilinearFail=false;
static size_t bilinearAllocationSize=0;
static void* bilinearAllocation=nullptr;
static std::vector<uint64_t> bilinearEvents;
static std::vector<WORD> bilinearHistory;
static void* xy_malloc(size_t size) {
    bilinearEvents.push_back(size);
    if(bilinearAllocation){std::abort();}
    if(bilinearFail || size>65536) {bilinearEvents.push_back(0);return nullptr;}
    bilinearAllocationSize=size;
    bilinearAllocation=std::malloc(size ? size : 1);
    if(!bilinearAllocation)std::abort();
    bilinearEvents.push_back(1);
    return bilinearAllocation;
}
static void xy_free(void* pointer) {
    if(pointer!=bilinearAllocation || !pointer)std::abort();
    bilinearEvents.push_back(2);
    const WORD* history=static_cast<const WORD*>(pointer);
    bilinearHistory.insert(bilinearHistory.end(),history,history+bilinearAllocationSize/sizeof(WORD));
    std::free(pointer);bilinearAllocation=nullptr;
}
