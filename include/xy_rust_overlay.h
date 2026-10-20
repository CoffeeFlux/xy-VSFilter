// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <vector>
namespace XyRust {
class OverlayOwner {
    uint64_t id_=0;
public:
    OverlayOwner()=default;~OverlayOwner(){Clear();}
    OverlayOwner(const OverlayOwner&);OverlayOwner& operator=(const OverlayOwner&);
    void Clear();void Take(uint64_t);
    void Swap(OverlayOwner& other){const uint64_t id=id_;id_=other.id_;other.id_=id;}
    void Import(const std::vector<uint8_t>&);
    std::vector<uint8_t> Read()const;
    uint64_t Variance(unsigned,unsigned)const;
    uint64_t BeBlur(float,float,float)const;
    void Offset(int,int);
};
}
