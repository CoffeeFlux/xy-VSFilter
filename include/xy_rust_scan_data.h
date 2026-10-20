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
    friend class RegionOwner;
    uint64_t id_;
    ScanOwner(const ScanOwner&)=delete;ScanOwner& operator=(const ScanOwner&)=delete;
public:
    ScanOwner();~ScanOwner();
    bool Convert(const PathOwner&,int,int);
    ScanSnapshot Read()const;
};
struct RegionSnapshot { int mWideBorder;std::vector<std::pair<uint64_t,uint64_t>> mWideOutline; };
class RegionOwner {
    uint64_t id_;
    RegionOwner(const RegionOwner&)=delete;RegionOwner& operator=(const RegionOwner&)=delete;
public:
    RegionOwner();~RegionOwner();
    bool Convert(const ScanOwner&,int,int);
    RegionSnapshot Read()const;
    uint64_t Raster(const ScanOwner&,int,int,int,int)const;
};
}
