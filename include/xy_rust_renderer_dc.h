// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Private experimental boundary. Status values match xy_rust_document.h.
// Buffers are unaligned little-endian packets; no Rust pointers cross the ABI.
// All owners share one DC in this module, including a null creation result.
// The thread that creates it must remain alive until final release. Callers
// serialize complete operations and never delete or retain borrowed HDC aliases.
uint32_t __cdecl xy_vsfilter_renderer_dc_acquire_v1(uint8_t* owner_out_8);
uint32_t __cdecl xy_vsfilter_renderer_dc_release_v1(uint64_t owner);
// begin returns token u64 + HDC u64. Owner changes/other operations refuse Busy
// until end. End only after all uses finish; stale or owner IDs cannot end it.
// Font calls use a separate registry and are permitted during this operation.
uint32_t __cdecl xy_vsfilter_renderer_dc_begin_v1(uint8_t* token_and_dc_out_16);
uint32_t __cdecl xy_vsfilter_renderer_dc_end_v1(uint64_t token);
#ifdef __cplusplus
}
#endif
