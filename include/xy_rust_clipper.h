// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <vector>
namespace XyRust {
class ClipRequest {
    uint64_t id_=0;
public:
    ClipRequest(int,int,bool,int,const int*,const double&,const double&);
    ~ClipRequest();
    ClipRequest(const ClipRequest&)=delete;ClipRequest& operator=(const ClipRequest&)=delete;
    uint64_t Id()const{return id_;}
};
class ClipMaskOwner {
    uint64_t id_=0;
public:
    ClipMaskOwner()=default;~ClipMaskOwner();
    ClipMaskOwner(const ClipMaskOwner&);ClipMaskOwner& operator=(const ClipMaskOwner&);
    void Take(uint64_t);
    void Swap(ClipMaskOwner& other){const auto id=id_;id_=other.id_;other.id_=id;}
    std::vector<uint8_t> Read()const;
};
}
