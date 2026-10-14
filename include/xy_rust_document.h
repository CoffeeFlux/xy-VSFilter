// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// EXPERIMENTAL PRIVATE v1 boundary. No production host ABI or HWND/HRESULT contract.
// All byte buffers use explicit little-endian values; never native struct layout.
#include <stddef.h>
#include <stdint.h>
#ifdef _MSC_VER
#define XY_DOCUMENT_CDECL __cdecl
#else
#define XY_DOCUMENT_CDECL
#endif
#ifdef __cplusplus
extern "C" {
#endif

enum {
    XY_DOCUMENT_V1_OK = 0, XY_DOCUMENT_V1_BAD_HANDLE = 1,
    XY_DOCUMENT_V1_BUSY = 2, XY_DOCUMENT_V1_INVALID_INPUT = 3,
    XY_DOCUMENT_V1_OUT_OF_RANGE = 4, XY_DOCUMENT_V1_COUNT_OVERFLOW = 5,
    XY_DOCUMENT_V1_UNRESOLVED_REFERENCE = 6
};
enum {
    XY_DOCUMENT_V1_ENTRY_HEADER_BYTES = 64,
    XY_DOCUMENT_V1_SEGMENT_HEADER_BYTES = 32,
    XY_DOCUMENT_V1_QUERY_BYTES = 16,
    XY_DOCUMENT_V1_COUNTS_BYTES = 8
};
// Buffer contract: nonzero input length/output capacity requires a live readable/
// writable range for that entire size, at most PTRDIFF_MAX. Bytes need no extra
// alignment. Null is permitted only with zero size. Inputs remain immutable until
// copied and outputs exclusively writable for the call; they may overlap each
// other because input is snapshotted before output. Neither may point into Rust
// resources. No pointer is retained. Invalid pointers are caller errors, not a
// status the implementation can diagnose. Capacity is validated before mutation;
// status errors leave output and document unchanged. Only successful result bytes
// are written; excess output capacity is untouched. Allocation failure remains
// governed by the existing allocator and is not a recoverable v1 status.
// IDs are typed, process-local, nonzero and never reused; stale/type-mismatched
// IDs return BAD_HANDLE. Admission is nonblocking; BUSY is private experimental
// behavior. No explicit host callbacks are invoked. Allocations/drops can enter
// CRT/new-handler code while admitted; compatible handling of that reentry is
// unresolved (Busy prevents overlapping loans). Snapshots survive owner close.
// create/snapshot_open/size return 8-byte LE u64 values.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_create_v1(uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_storage_v1(uint64_t destination, uint64_t source);
// Storage copy and entry/segment clear retain the destination default style.
// Default style starts absent; the native constructor supplies initialized bytes.
// Style wire: 192-byte LE header: five i32 scalars (charset/weight/alignment/
// border/relative-to)@0, four i32 margins(left/top/right/bottom)@20,
// u32 colors[4]@36, alpha bytes[4]@52, bool bytes italic/underline/strikeout@56,
// reserved-zero bytes59..64, font u16-unit count:u32@64, reserved-zero68..72,
// fifteen f64 bit patterns@72 (size, outlineX/Y, shadowX/Y, scaleX/Y, spacing,
// blur, gaussianBlur, angleZ/X/Y, shiftX/Y), then raw UTF16LE font units.
// Count <=INT32_MAX; exact total length required. No native defaults/layout or
// floating-point normalization. Retired values are destroyed after admission.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_default_style_v1(uint64_t owner, const uint8_t*, size_t);
// Explicit default assignment; source absence clears destination; self-copy no-op.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_default_style_v1(uint64_t destination, uint64_t source);

uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_destroy_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_counts_v1(uint64_t, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_entry_header_v1(uint64_t, uint32_t index, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_segment_header_v1(uint64_t, uint32_t index, uint8_t*, size_t);
// Query modes: 0 entry, 1 next segment, 2 containing segment; integer TIME only.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_query_time_v1(uint64_t, uint32_t mode, int64_t time, uint8_t*, size_t);
// Snapshot kinds: 0 full entry wire, 1 full segment wire, 2 initialized default
// style (index must be zero; absent defaults return OUT_OF_RANGE). Full entry wire is the
// 64-byte header followed by text/style/actor/effect raw UTF-16LE arrays. Full
// segment wire is the 32-byte header followed by u32LE member indexes.
// Entry header offsets: start:i64@0, end:i64@8, margins:i32[4]@16
// (left/top/right/bottom), layer:i32@32, readorder:i32@36, unicode:u32@40,
// field lengths:u32[4]@44 (code units), reserved-zero:u32@60.
// Segment header offsets: start:i64@0, end:i64@8, animated:u32@16,
// member_count:u32@20, reserved-zero:u32[2]@24. Booleans are exactly 0 or 1.
// Query header: index:i32@0, found:u32@4, count:u32@8, reserved-zero:u32@12.
// Counts: entry_count:u32@0, segment_count:u32@4. Counts/lengths/indexes are
// bounded by INT32_MAX; byte sizes use checked arithmetic. No terminators are
// included in strings and embedded zero code units contribute to their lengths.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_snapshot_open_v1(uint64_t, uint32_t kind, uint32_t index, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_snapshot_size_v1(uint64_t snapshot, uint8_t*, size_t);
// Reads exactly length bytes at offset, or returns OUT_OF_RANGE without output.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_snapshot_read_v1(uint64_t snapshot, uint64_t offset, uint8_t*, size_t length);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_snapshot_drop_v1(uint64_t snapshot);
// Add modes: 0 Add, 1 entry-only, 2 WebVTT; record is already natively prepared.
// Result: applied:u32, index:i32 (-1 when not applied), total 8 bytes.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_add_prepared_v1(uint64_t, uint32_t mode, const uint8_t*, size_t, uint8_t*, size_t);
// Prepared text/style are raw UTF-16LE (even byte lengths). No native conversion,
// normalization, style-map decisions or implicit rebuild. Flag -1 retains stored
// Unicode mode, 0 stores false, 1 true, after caller's native preparation.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_prepared_text_v1(uint64_t, uint32_t index, int32_t flag, const uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_prepared_style_v1(uint64_t, uint32_t index, const uint8_t*, size_t);
// Entry-style rename only: case-sensitive native CString equality terminates at
// the first NUL code unit on each side; assignment preserves the entire new raw
// UTF-16LE value, including its NUL suffix. Neither style-map mutation nor rebuild.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_rename_style_cstring_v1(uint64_t, const uint8_t* old_units, size_t old_bytes, const uint8_t* new_units, size_t new_bytes);
// Endpoint: 0=end, 1=start. Caller supplies native-evaluated time; no rebuild.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_entry_time_v1(uint64_t, uint32_t index, uint32_t start, int64_t value);
// Native-selected original indexes (u32LE bijection) or native-evaluated start/end
// pairs (i64LE). Exact entry count is required. Success rebuilds even identity.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_reorder_entries_v1(uint64_t, const uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_replace_entry_times_v1(uint64_t, const uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_rebuild_segments_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_clear_storage_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_remove_entries_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_animation_or_v1(uint64_t, uint32_t segment, uint32_t animated);
// No rebuild. Result: applied:u32, reserved-zero:u32, end:i64 (zero if absent).
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_correct_last_end_v1(uint64_t, int64_t bound, uint8_t*, size_t);
#ifdef __cplusplus
}
#endif
#undef XY_DOCUMENT_CDECL
