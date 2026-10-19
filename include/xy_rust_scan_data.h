// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <utility>
#include <vector>
namespace XyRust {
class PathOwner;
// Detached values for native consumers; no canonical Rust address is exported.
struct ScanSnapshot {
    int mWidth,mHeight;unsigned mEdgeNext,mEdgeHeapSize;bool fFirstSet;
    CPoint firstp,lastp;std::vector<std::pair<uint64_t,uint64_t>> mOutline;
};
class ScanOwner {
    uint64_t id_;
    ScanOwner(const ScanOwner&)=delete;ScanOwner& operator=(const ScanOwner&)=delete;
public:
    ScanOwner();~ScanOwner();
    bool Convert(const PathOwner&,int,int);
    ScanSnapshot Read()const;
};
}
