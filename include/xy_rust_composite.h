// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
namespace XyRust {
class OverlayOwner;class ClipMaskOwner;
class CompositeOwner {
    uint64_t id_=0;
public:
    CompositeOwner()=default;~CompositeOwner();
    CompositeOwner(const CompositeOwner&)=delete;CompositeOwner& operator=(const CompositeOwner&)=delete;
    static std::shared_ptr<CompositeOwner> Build(const OverlayOwner&,const ClipMaskOwner*,const int32_t*,const uint32_t*);
    std::vector<uint8_t> Read()const;
};
}
