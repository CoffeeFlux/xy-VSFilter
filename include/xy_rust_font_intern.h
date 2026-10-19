// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <cstdint>
#include <cstddef>
namespace XyRust {
struct FontSnapshot {
    HFONT handle;
    int m_ascent,m_descent;
    operator HFONT()const{return handle;}
};
class FontIntern {
    struct Owner;
    std::shared_ptr<Owner> owner_;
public:
    FontIntern(const STSStyleBase& style);
    FontSnapshot get()const;
    static size_t Count();
};
}
