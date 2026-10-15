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
    XY_DOCUMENT_V1_COUNTS_BYTES = 8,
    XY_DOCUMENT_V1_STYLE_RENAME_HEADER_BYTES = 16,
    XY_DOCUMENT_V1_SORT_HEADER_BYTES = 8, XY_DOCUMENT_V1_SORT_RECORD_BYTES = 16,
    XY_DOCUMENT_V1_TIME_HEADER_BYTES = 8, XY_DOCUMENT_V1_TIME_RECORD_BYTES = 16
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
// Selected Copy state AFTER caller's virtual Empty: name, mode/type/encoding,
// script size/wrap/collisions/scaled, default style, forced-default. Source optional
// absence is copied explicitly (native constructor normally initializes it).
// Destination path/PAR/colors/named map/records remain untouched. No callbacks;
// retired name/default payloads are dropped after admission. Typed self-copy no-op.
// Caller still performs native CopyNamedStylesFrom(false), then prepared copy.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_selected_state_v1(uint64_t destination, uint64_t source);

uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_destroy_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_counts_v1(uint64_t, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_entry_header_v1(uint64_t, uint32_t index, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_segment_header_v1(uint64_t, uint32_t index, uint8_t*, size_t);
// Query modes: 0 entry, 1 next segment, 2 containing segment; integer TIME only.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_query_time_v1(uint64_t, uint32_t mode, int64_t time, uint8_t*, size_t);
// Base Empty: reset only screen size(0,0), clear named map retaining bins,
// detach segments then entries; retire style values, segments, entries in that
// order AFTER admission. Other metadata/default/identity remains untouched.
// Caller retains virtual Empty/Deinit ordering. Map key/node drops and allocation
// reentry remain inside map mutation; this does not claim native deletion timing.
// Diagnostic clear_storage/named_style_clear and Copy's separate clear remain.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_empty_v1(uint64_t owner);
// Narrow prepared conflict relocation: header16 old-unit-count:u32@0,
// new-unit-count:u32@4, previous-style-byte-length:u64@8, then old/new raw
// UTF16LE arrays and one exact initialized style wire. Native classification,
// equality and CRT naming remain outside; incoming Put/delete stays after its
// native conflict scope. Operation: remove(old), put(new,captured previous),
// rename matching entry styles (CString prefix comparison, full replacement).
// Aliased keys/existing new keys/missing old keys follow those operations;
// no stale-plan retry. Input/count errors leave owner unchanged. Retired style
// values/entry names drop after admission; map keys/allocations still occur inside.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_commit_style_rename_v1(uint64_t owner, const uint8_t* packet, size_t packet_bytes);
// Render metadata fields:0 PAR type,1 color matrix,2 color range (raw i32),
// 3 forced default style(strict0/1),4 supplied-only LCID(raw u32 bits carried
// through the i32 setter argument). No constructor LCID default. Clone copies
// LCID; selected Copy/Empty/storage operations retain destination LCID.
// Other fields are initially absent until native initialization.
// Get writes4 LE bytes; absence => UNRESOLVED_REFERENCE, output unchanged.
// PAR compensation is separately supplied/stored as opaque u64 double bits;
// get writes8 LE bytes. No floating-point evaluation/normalization occurs here.
// Prepared copy/clear retain all fields; native full Copy assigns only the
// forced-default field and retains destination PAR/color at its original points.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_metadata_v1(uint64_t owner, uint32_t field, int32_t value);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_get_metadata_v1(uint64_t owner, uint32_t field, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_par_bits_v1(uint64_t owner, uint64_t bits);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_get_par_bits_v1(uint64_t owner, uint8_t*, size_t);
// Script fields:0 width,1 height,2 wrap,3 collisions (raw signed i32),4 scaled
// borders/shadows (strict0/1); 5 layout width, 6 layout height (raw i32).
// Initially absent until native initialization. Layout is separate from script size;
// selected-state Copy, Empty and prepared-storage operations retain layout.
// Absent get => UNRESOLVED_REFERENCE with unchanged output. Scalar get writes4
// LE bytes; size get writes8 (width,height) in one admission. Size set changes
// only width/height in one admission. Storage copy/clear retain all fields;
// native Empty explicitly resets the size at its original assignment point.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_script_field_v1(uint64_t owner, uint32_t field, int32_t value);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_get_script_field_v1(uint64_t owner, uint32_t field, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_script_size_v1(uint64_t owner, int32_t width, int32_t height);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_get_script_size_v1(uint64_t owner, uint8_t*, size_t);
// Supplies both raw layout axes in one admission; does not alter script size.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_layout_size_v1(uint64_t owner, int32_t width, int32_t height);
// Interpretation fields: 0 mode, 1 subtitle type, 2 encoding. Initially absent;
// the native constructor supplies its enum constants. Raw signed i32 values are
// not validated/normalized. Get writes4-byte LE i32; absent => UNRESOLVED_REFERENCE
// with output untouched. Entry/segment copy/clear retain all interpretation fields.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_interpretation_v1(uint64_t owner, uint32_t field, int32_t value);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_get_interpretation_v1(uint64_t owner, uint32_t field, uint8_t*, size_t);
// Identity fields: 0 name, 1 path. Initially empty; raw UTF16LE units retain
// all embedded NULs/surrogates. Entry/segment copy/clear retain both identities.
// Native full Copy/Open orchestration assigns fields explicitly at native points.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_identity_v1(uint64_t owner, uint32_t field, const uint8_t*, size_t);
// Named table values are supplied initialized style wires; zero style bytes
// mean a present null value. Raw UTF16LE keys use LPCWSTR first-NUL semantics
// for lookup/hash/stored initial key. Count output is one u32LE (capacity >=4).
// Entry/segment copy/clear retain destination named storage; explicit clear
// returns no values and destroys retired style payloads after admission.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_named_style_count_v1(uint64_t owner, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_named_style_put_v1(uint64_t owner, const uint8_t* key, size_t key_bytes, const uint8_t* style_wire, size_t style_bytes);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_named_style_remove_v1(uint64_t owner, const uint8_t* key, size_t key_bytes);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_named_style_clear_v1(uint64_t owner);
// Lookup output16: state:u32@0 (0 missing/1 null/2 value), reserved-zero:u32@4,
// snapshot-id:u64@8. Request exactly0/1; ID nonzero only for Value+request1.
// Output capacity is checked before snapshot creation; errors leave output intact.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_named_style_lookup_v1(uint64_t owner, const uint8_t* key, size_t key_bytes, uint32_t snapshot_requested, uint8_t*, size_t);
// Snapshot kinds: 0 full entry wire, 1 full segment wire, 2 initialized default
// style (index zero; absent defaults return OUT_OF_RANGE), 3 ordinal named key
// (raw UTF16LE), 4 ordinal named value wire (null => UNRESOLVED_REFERENCE).
// Kinds5 name/6 path are raw UTF16LE identity snapshots (index0 only).
// Kind7 scalar Sort snapshot(index0): header count:u32@0/reserved-zero:u32@4,
// then16-byte rows start:i64@0/layer:i32@8/readorder:i32@12; ordinal implicit.
// No text cloning or Rust sorting; native adapter must retain actual STSEntry
// width/comparators. Counts and total snapshot sizes use checked native bounds.
// Kind8 resolved entry style wire (present-null => UNRESOLVED_REFERENCE).
// Kind9 resolved charset: state:u32@0 (1 null/2 value), charset:i32@4;
// null carries -1, distinct from a Value whose charset is -1.
// Both resolve forced default BEFORE index validation; otherwise entry name,
// then internal Default key only on missing, then owned default only on missing.
// Missing supplied forced/default state => UNRESOLVED_REFERENCE. Index is rawu32;
// forced selection bypasses entry bounds. Replies are immutable owned snapshots.
// Kind10 layout snapshot(index0): width:i32@0/height:i32@4; either absent
// axis => UNRESOLVED_REFERENCE. Native constructor supplies CSize() explicitly.
// Kind11 scalar time snapshot(index0): count:u32/reserved-zero:u32 then
//16-byte start:i64/end:i64 rows, no text cloning.
// Missing ordinals or nonzero identity/Sort/layout/time indexes return OUT_OF_RANGE. Full entry wire is the
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
// Add modes 0 Add, 1 entry-only, 2 WebVTT retain diagnostic prepared records.
// Modes 3 AddSuffix, 4 EntryOnlySuffix, 5 WebVttSuffix apply owned final CString
// CR removal, LF replacement and style normalization before persistent Add.
// Native initial trimming/rejection/WebVTT parsing remains before this suffix.
// No empty-text recheck after suffix; unresolved allocation-tail input refuses
// before document mutation. All other raw fields and flags are retained.
// Result: applied:u32, index:i32 (-1 when not applied), total 8 bytes.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_add_prepared_v1(uint64_t, uint32_t mode, const uint8_t*, size_t, uint8_t*, size_t);
// Read-only text/encoding plan. Modes0WideSSA/1WideStrip/2PackedSSA/
// 3PackedStrip/4ConvertWide/5ConvertPacked. Output8 receives snapshot ID.
// Packet32: action:u32(0Keep/1ToWide/2ToPacked/3NoOp),target_unicode:u32,
// strip_tags:u32,has_style:u32,charset:i32,reserved-zero:u32,units:u64,
// then full raw UTF-16LE text. NoOp has no text/style and charset0.
// Native charset conversion/SSA stripping remains outside admission; conversion
// commit uses existing prepared text setter. Missing style carries charset-1.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_text_plan_v1(uint64_t owner,uint32_t index,uint32_t mode,uint8_t*,size_t);
// Read-only SetStr plan: input flag is 0/1, raw input is UTF-16LE. Output8
// receives the same packet32 snapshot. Entry flag is read before LF-only
// preparation; CR remains. Target is the stored flag. Equal flags return Keep,
// has_style0/charset0, bypassing native charset assertion/conversion. Differing
// flags resolve charset after preparation. Commit text with flag -1 to retain
// stored Unicode state. No generation guard or mutation is performed here.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_set_text_plan_v1(uint64_t owner,uint32_t index,uint32_t input_unicode,const uint8_t*,size_t,uint8_t*,size_t);
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
// Native adapter guards same mode before CRT arithmetic. Target0TIME/1FRAME;
// complete start/end pairs are staged before admission. Output u64LE applied:
//0 same-mode(no rebuild/animation changes),1 installed pairs+mode then rebuilt.
// Current mode must be supplied. Count mismatch leaves owner/output unchanged.
// Callback and optional logging belong after successful applied reply/unlock.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_commit_time_conversion_v1(uint64_t owner, uint32_t target, const uint8_t* pairs, size_t bytes, uint8_t* applied, size_t capacity);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_rebuild_segments_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_clear_storage_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_remove_entries_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_animation_or_v1(uint64_t, uint32_t segment, uint32_t animated);
// No rebuild. Result: applied:u32, reserved-zero:u32, end:i64 (zero if absent).
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_correct_last_end_v1(uint64_t, int64_t bound, uint8_t*, size_t);
// CopyStyles typed run binds destination and source. Open performs no mutation;
// first step initializes/clears and returns Advanced before reading source rows.
// Step output8 is snapshot ID; packet16: action:u32(0Done/1Advanced/2NeedSuffix),
// reserved-zero:u32,units:u64, then normalized full raw UTF-16LE name.
// Submit receives native CRT candidate and returns applied:u64LE. No style wire
// or native style temporary crosses the boundary per copied row.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_styles_open_v1(uint64_t destination,uint64_t source,uint32_t append,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_styles_step_v1(uint64_t run,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_styles_submit_v1(uint64_t run,const uint8_t*,size_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_copy_styles_drop_v1(uint64_t run);
// Coarse AddStyle: nullable wire is zero bytes. Begin output16 holds
// snapshotID:u64,runID:u64(0Done), before caller constructs native values.
// Begin snapshot packet24:
// action:u32(0Done/1Conflict),reserved-zero:u32,run:u64(0Done),units:u64,
// normalized full raw UTF-16LE name (also supplied for Done).
// Submit preserves occupied candidates including null; applied:u64LE 0/1.
// Native CRT candidate naming and native temporary destruction stay outside ABI.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_add_style_begin_v1(uint64_t owner,const uint8_t* name,size_t name_bytes,const uint8_t* style,size_t style_bytes,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_add_style_submit_v1(uint64_t run,const uint8_t* candidate,size_t bytes,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_add_style_drop_v1(uint64_t run);
// Typed unknown-style pass bound to a live owner. Each step returns an immutable
// snapshot: kind:u32(0 Done/1 Advanced/2 Warning), reserved-zero:u32, units:u64,
// then full raw UTF-16LE warning name. Release registry admission before dialogs.
// Resume accepts only 0/1, reacquires owner and rereads current row style.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_unknown_open_v1(uint64_t owner, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_unknown_step_v1(uint64_t run, uint8_t*, size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_unknown_resume_v1(uint64_t run, uint32_t accepted);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_unknown_drop_v1(uint64_t run);
#ifdef __cplusplus
}
#endif
#undef XY_DOCUMENT_CDECL
