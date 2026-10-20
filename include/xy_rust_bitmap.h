// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <vector>
namespace XyRust {
class OverlayOwner;class CompositeOwner;
class BitmapOwner {
    uint64_t id_=0;
public:
    BitmapOwner()=default;~BitmapOwner();
    BitmapOwner(const BitmapOwner&)=delete;BitmapOwner& operator=(const BitmapOwner&)=delete;
    void Create(const int32_t*);
    void Draw(const OverlayOwner&,const CompositeOwner*,const int32_t*,const uint32_t*);
    void Action(uint32_t,uint32_t);
    std::vector<uint8_t> Read()const;
};
}
