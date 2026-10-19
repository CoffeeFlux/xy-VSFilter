// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RustDocumentOwner.h"
#include "xy_rust_renderer_dc.h"

namespace XyRust {
inline uint64_t AcquireRendererDc() {
    std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_renderer_dc_acquire_v1(out.data());}));
    return DocumentDetail::Load(out,0,8);
}
inline void ReleaseRendererDc(uint64_t& owner) noexcept {
    const uint64_t id=owner;owner=0;
    DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_renderer_dc_release_v1(id);}));
}
// The module's recursive adapter gate covers the ENTIRE native text/path call,
// including font destruction. Individual Rust registry admissions remain short.
// Lock order: native gate -> DC admission (ends on return); then DOCUMENTS for
// font calls while the DC operation token remains active, with no DC state loan.
// No host callback runs here. Reentrant DC operations refuse rather than share
// mutable GDI selection/path state. Host object lifetime still bounds callers.
class RendererDcOperation {
    DocumentDetail::Loan gate_;
    uint64_t token_;
    HDC dc_;
public:
    RendererDcOperation():token_(0),dc_(NULL) {
        std::array<uint8_t,16> out;
        DocumentDetail::Check(xy_vsfilter_renderer_dc_begin_v1(out.data()));
        token_=DocumentDetail::Load(out,0,8);
        dc_=reinterpret_cast<HDC>(static_cast<uintptr_t>(DocumentDetail::Load(out,8,8)));
    }
    ~RendererDcOperation(){DocumentDetail::Release(xy_vsfilter_renderer_dc_end_v1(token_));}
    HDC get() const {return dc_;}
    RendererDcOperation(const RendererDcOperation&)=delete;
    RendererDcOperation& operator=(const RendererDcOperation&)=delete;
};
}
