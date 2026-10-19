// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <cstdint>
#include <cstddef>
namespace XyRust {
// Transitional native lifetime only. Canonical styles and rectangles are Rust-owned.
class StyleIntern {
    struct Owner;
    std::shared_ptr<Owner> owner_;
    uint64_t Identity()const;
public:
    StyleIntern();
    StyleIntern(const STSStyle& value);
    StyleIntern& operator=(const STSStyle& value);
    STSStyle get()const;
    operator STSStyle()const;
    friend bool operator==(const StyleIntern& a,const StyleIntern& b) {return a.Identity()==b.Identity();}
    friend bool operator!=(const StyleIntern& a,const StyleIntern& b) {return !(a==b);}
    static size_t Count();
};
}
