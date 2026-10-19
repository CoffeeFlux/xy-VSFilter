// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <cstdint>
#include <cstddef>
namespace XyRust {
// Transitional native lifetime wrapper; canonical rectangle values live in Rust.
class RectIntern {
    struct Owner;
    std::shared_ptr<Owner> owner_;
    uint64_t Identity()const;
public:
    RectIntern();
    RectIntern(const CRect& value);
    RectIntern& operator=(const CRect& value);
    CRect get()const;
    operator CRect()const {return get();}
    friend bool operator==(const RectIntern& a,const RectIntern& b) {return a.Identity()==b.Identity();}
    friend bool operator!=(const RectIntern& a,const RectIntern& b) {return !(a==b);}
    static size_t Count();
};
}
