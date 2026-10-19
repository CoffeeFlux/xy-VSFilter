// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <vector>
namespace XyRust {
// Detached temporary values, never aliases of Rust storage. Native transforms
// explicitly Replace after editing; readers cannot retain a canonical pointer.
struct PathSnapshot {std::vector<POINT> points;std::vector<uint8_t> types;};
class PathOwner {
    uint64_t id_;
public:
    PathOwner();PathOwner(const PathOwner&);
    PathOwner& operator=(const PathOwner&);
    ~PathOwner();
    int Count()const;ULONG Hash()const;bool Equal(const PathOwner&)const;
    PathSnapshot Read()const;void Replace(const PathSnapshot&);
    void Clear();bool Begin(HDC,bool);bool End(HDC,bool,long,long);
    void Align(CPoint*,CSize*);
    bool Text(HDC,uintptr_t,uint64_t,const uint16_t*,size_t);
};
}
