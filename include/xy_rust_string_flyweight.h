// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <cstdint>
#include <cstddef>
namespace XyRust {
// Transitional native lifetime wrapper. Canonical stored text and cache entries
// live in Rust. Get returns a detached CString, never a reference into an owner.
class StringFlyweight {
    struct Owner;
    std::shared_ptr<Owner> owner_;
    // The legacy scalar remains readable after an implicit move even though
    // the source no longer owns text. Copy/move operations retain this scalar.
    std::size_t legacy_id_;
public:
    typedef std::size_t IdType;
    static const int INVALID_ID=0;
    explicit StringFlyweight(const CStringW* value);
    CStringW Get()const;
    IdType GetId()const;
    class Cacher {
        static size_t Call(uint32_t action,size_t maximum,bool reset,unsigned field);
    public:
        ~Cacher();
        size_t GetMaxItemNum()const;
        size_t GetCurItemNum()const;
        size_t GetCacheHitCount()const;
        size_t GetQueryCount()const;
        size_t SetMaxItemNum(size_t maximum,bool reset=false);
        void RemoveAll(bool reset=false);
        void RemoveTail();
    };
    static Cacher* GetCacher();
};
}
