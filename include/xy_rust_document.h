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
// 5 simple subtitle(strict0/1), also initially absent. Open success-header
// acknowledgement sets it from parser index > 0 before Sidecar/Rebuild.
// Clone copies simple; selected Copy/Empty/storage retain destination simple.
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
// Pure SaveAs fixed section text. Sections 0 Header, 1 Events, 2 Footer,
// 3 SidecarHeader; formats 0..13 valid. Scaled is 0/1; native retains when to
// request/write sections and file/codepage services. Output8 receives snapshot
// ID; snapshot contains raw UTF-16LE with no header.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_format_export_section_v1(uint32_t format,uint32_t section,int32_t width,int32_t height,int32_t collisions,uint32_t scaled,uint8_t*,size_t);
// Pure SaveAs body row formatting. Formats 0..5 SRT/SUB/SMI/PSB/SSA/ASS;
// valid formats 6..13 produce empty rows; >=14 refuses. Native supplies integer
// arithmetic results and already converted/newline-prepared text in Entry64.
// Each string field uses raw UTF-16 up to its first NUL, matching native %s.
// Output8 receives snapshot ID; snapshot bytes are raw UTF-16LE without header.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_format_export_row_v1(uint32_t format,int32_t sequence,int32_t start_ms,int32_t end_ms,int32_t frame_start,int32_t frame_end,int32_t margin_v,const uint8_t* input,size_t bytes,uint8_t*,size_t);
// Translated search run, modes 0 Entry, 1 NextSegment, 2 ContainingSegment.
// Begin output32 is runID:u64 then state24. Supply output24 is six LE words:
// pending:u32,start:u32,requestIndex:i32,resultIndex:i32,found:u32,count:u32.
// Native evaluates exactly each requested timestamp between calls. No owner
// borrow crosses translation; selected count is frozen, bound owner stays live.
// Supplying after completion refuses without changing run or output.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_search_begin_v1(uint64_t owner,uint32_t mode,int64_t time,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_search_supply_v1(uint64_t run,int64_t value,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_search_drop_v1(uint64_t run);
// Open lifecycle control only; all parsing, dialogs, virtual callbacks and
// ordered success-header publication remain native. Begin binds a live owner without
// mutation; parser count must fit INT_MAX. Step output8 is action:u32,index:u32.
// Ack -2 reads current action, -1 acknowledges non-parse, 0/1 parser result.
// Actions: 0 InitialEmpty, 1 CapturePosition, 2 Parse, 3 SyntaxWarning, 4 Seek,
// 5 RetryEmpty, 6 AbortEmpty, 7 SuccessHeader, 8 Sidecar, 9 Rebuild,
// 10 UnknownStyles, 11 DoneFalse, 12 DoneTrue. Index is used only by Parse/Header.
// Acknowledging UnknownStyles applies the live both-zero screen fallback; no
// callback executes under admission. Missing screen leaves the phase unchanged.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_open_lifecycle_begin_v1(uint64_t owner,uint32_t parser_count,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_open_lifecycle_step_v1(uint64_t run,int32_t ack,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_open_lifecycle_drop_v1(uint64_t run);
// ASS header run binds a document and initializes BT601/TV at Begin. Step
// commands:0 lowered key,1 GetInt result,2 GetInt failure,3 detected script
// version(0,4,5,6),4 extracted/lowered value. Text is raw UTF-16LE bytes;
// 5 starts dialogue parsing,6 supplies a numeric token result,7 commits with
// native-trimmed text and flags(bit0 Unicode,bit1 native Default comparison).
// Scalar commands require empty text; other text commands require scalar zero.
// Reply: kind/version/style-version/accepted as four LE i32. A numeric parse
// failure commits the legacy size/wrap reset and makes the run terminal.
// Invalid protocol/capacity leaves the run, document and output untouched;
// absent opposite screen axis reports UNRESOLVED_REFERENCE. No native loans
// or callbacks survive a call. Drop remains valid after document destruction.
// Checked USF conversion. Begin packet: counted raw UTF16 language; style/text
// counts:u32; styles: name,13 font strings(face,size,color[4],weight,italic,
// underline,alpha,outline,shadow,wrap),7 position strings(alignment,relative-to,
// horizontal/vertical margins,rotate z/x/y); texts: start/stop:i32,effect/style/
// text,7 position strings. Each string is count:u32 then raw u16 units.
// Step command equals op:1 CompareNoCase,2 wcstol10,3 ColorToDWORD,4 Margin,
// 5 Alignment,6 AddStyle ack0,7 Add ack0. Done8. Group0 op/arg/index/aLen/bLen:u32
// and raw strings; group1 existing style wire; group2 existing entry wire.
// Persistent USF source. Only successful XML loads begin a new traversal.
// Request: op,node,result,text-length:u32 + raw u16. Services: 1 lower name
// plus nodeValue access; 2 firstChild; 3 nextSibling; 4 attribute; 5 text;
// 6 raw XML; 7 TimeToInt; 8 wcstol10; 9 ColorToDWORD; 10 CompareNoCase(Default);
// 11 done (result=accepted). Node1 is the loaded DOM, node0 is null. Replies
// carry either value:i32 or raw u16, never pointers. Group1 snapshots all model
// fields in probe order. Source and destination lifetimes are checked on every
// conversion step. Failed native XML loads do not clear or replace this source.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_source_create_v1(uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_source_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_source_begin_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_source_step_v1(uint64_t,uint32_t,int32_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_source_part_v1(uint64_t,uint32_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_begin_v2(uint64_t,uint64_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_begin_v1(uint64_t,const uint8_t*,size_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_step_v1(uint64_t,uint32_t,int32_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_usf_part_v1(uint64_t,uint32_t,uint8_t*,size_t);
// Checked RealText engine. Begin retains CRealTextParser's private defaults.
// Read copies UTF16LE and strict-bool spaces. Step command equals pending op:
// 1 CompareNoCase(i32),2 iswspace(bool),3 towlower(u16),4 _wtoi(i32),
// 5 iswdigit(bool),6 Add ack(0). No native callback/pointer survives a call.
// Part0 op/accepted:u32,start/end:i64,unit/index:u32,raw UTF16LE payload;
// Part1 tag count:u32 and tags; Part2 count:u32 then start/end:i32,length:u32,
// units; Part3 center:u32/window tag. Tag: flags/name length/attribute count:u32,
// name units; sorted attribute key/value lengths:u32 and raw units.
// Ops Read0/Compare1/Space2/Lower3/Integer4/Digit5/Emit6/Done7/Failed8/At9.
// At9 requests the original bounds-checked native read of payload[index].
// This private protocol replaces v1; public DLL exports are unchanged.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_realtext_begin_v2(uint64_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_realtext_drop_v2(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_realtext_read_v2(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_realtext_step_v2(uint64_t,uint32_t,int32_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_realtext_part_v2(uint64_t,uint32_t,uint8_t*,size_t);
// Checked WebVTT session. Read copies raw UTF16LE and strict-bool whitespace.
// Packet0 header count and three strings only for count2/3; Packet1 converted
// UTF16LE; Packet2 key/foreground/background (value0 for commands1/2).
// Three strings: three LE u32 unit counts followed by the raw spans.
// Time: count5 first three values plus zero; count7 all four; otherwise zeros.
// Step0 style extraction ack,1 Add ack. Part0: op/accepted:u32,start/end:i64,
// raw payload. Part1 raw cueTags; Part2 count:u32 then three-string packets.
// Ops Magic0/Header1/StyleRead2/StyleParse3/HeaderScan4/Start5/End6/Caption7/
// Convert8/Emit9/Done10/Failed11. Sessions retain no native pointers/callbacks.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_begin_v1(uint64_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_read_v1(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_packet_v1(uint64_t,uint32_t,int32_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_time_v1(uint64_t,int32_t,int32_t,int32_t,int32_t,int32_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_step_v1(uint64_t,uint32_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_vtt_part_v1(uint64_t,uint32_t,uint8_t*,size_t);
// Checked SAMI discovery/rewind/caption session. Read/Upper copy raw UTF16LE.
// Step0 rewind ack,1 strict-bool iswdigit,2 Add ack; acknowledgements use value0.
// Part: op/accepted/digit-unit/reserved:u32[4],rewind-position:u64,start/end:i64,
// raw UTF16LE. Ops ProbeRead0/ProbeUpper1/ProbeDiscard2/Rewind3/Read4/Upper5/
// Digit6/Emit7/Done8/Failed9. Discovery deliberately reads one extra line.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_sami_begin_v1(uint64_t,uint64_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_sami_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_sami_read_v1(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_sami_upper_v1(uint64_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_sami_step_v1(uint64_t,uint32_t,uint32_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_sami_part_v1(uint64_t,uint8_t*,size_t);
// Checked MicroDVD session. Read supplies raw UTF16LE and strict-bool whitespace.
// Step0 primary scan,1 fallback scan BEFORE +1,2 Add ack (zero scalars).
// Style acknowledges native AddStyle with copied normalized name and charset.
// Part0: op/accepted/charset/reserved:u32[4],start/end:i64,raw UTF16LE.
// Ops: Read0/Primary1/Fallback2/Style3/Emit4/Done5/Failed6.
// Part1: raw UTF16LE candidate name during Style, current style otherwise.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_microdvd_begin_v1(uint64_t,int32_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_microdvd_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_microdvd_read_v1(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_microdvd_step_v1(uint64_t,uint32_t,int32_t,int32_t,int32_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_microdvd_style_v1(uint64_t,uint32_t,int32_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_microdvd_part_v1(uint64_t,uint32_t,uint8_t*,size_t);
// Checked text-parser session:0 SubRip/1 MPL2/2 OldSubRip/3 VPlayer. Read supplies EOF,
// raw UTF16LE and strict-bool CString predicates. Step0 supplies scanf count
// and eight initialized i32 LE values; step1 numeric-only bool; step2 Add ack.
// Step3 supplies FindChar index (-1 absent), no input bytes.
// Never serialize uninitialized native locals on partial scanf conversion.
// Part: op:u32(0..7),accepted:bool32,start:i64,end:i64,raw UTF16LE.
// Op7 FindPipe alone prefixes raw UTF16LE with a u32 search position.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_text_parser_begin_v1(uint64_t,uint32_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_text_parser_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_text_parser_read_v1(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_text_parser_step_v1(uint64_t,uint32_t,int32_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_text_parser_part_v1(uint64_t,uint8_t*,size_t);
// Checked XombieSub session; all byte buffers follow the common contract.
// Step0 classify,1 text,2 i32 bits,3 f64 bits,4 acknowledge,5 numeric failure.
// Read supplies EOF=!ReadString, raw UTF16LE and one strict-bool whitespace flag
// per unit. Part0: op/sep/kind/accepted:u32[4],raw UTF16LE;1 style wire;
// 2 raw style name;3 entry wire;4 decoded font bytes. Callbacks occur after copy-out.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_xombie_begin_v1(uint64_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_xombie_drop_v1(uint64_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_xombie_read_v1(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_xombie_step_v1(uint64_t,uint32_t,uint64_t,const uint8_t*,size_t,const uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_xombie_part_v1(uint64_t,uint32_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_begin_v1(uint64_t owner,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_step_v1(uint64_t run,uint32_t command,int32_t value,const uint8_t*,size_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_drop_v1(uint64_t run);
// Dialogue part returns a snapshot ID. Snapshot: request:u32,separator:u32,
// raw UTF-16LE. Groups:0 next numeric token or completion,1 stripped style,
// 4 text before native FastTrim. Groups1/4 require completed numeric parsing.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_dialogue_part_v1(uint64_t run,uint32_t group,uint8_t*,size_t);
// Line ownership/dispatch: ReadString result supplies EOF(strict0/1), raw UTF-16LE
// and one strict-bool whitespace byte per unit. Both buffers are copied before
// admission. Input-part returns a snapshot: action:u32 (Read0,LowerKey1,Record2,
// Done3),reserved:u32=0,raw UTF-16LE. Group0 is key when LowerKey (otherwise empty);
// group1 is payload in Record only. Step command8 acknowledges a record with
// scalar0=failure/1=success and empty text. A final blank/comment requests another
// read; a final completed record stops. Invalid protocol leaves output unchanged.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_read_v1(uint64_t run,uint32_t eof,const uint8_t*,size_t,const uint8_t*,size_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_input_part_v1(uint64_t run,uint32_t group,uint8_t*,size_t);
// Style session uses the checked ASS run ID. Commands0 begin(input),1 i32 bits,
// 2 f64 bits,3 raw string value,4 release finished value. Commands1..3 also supply
// the native remaining GetStr/GetInt/GetFloat buffer. Inputs are raw UTF-16LE;
// output is the standard16-byte ASS state. Part groups:0 field:u32,reserved:u32,
// remainder;1 same header plus name;2 complete existing style wire. Groups1/2
// require all fields. Snapshots are independent; no native style pointer crosses.
// ASS step commands9/10/11: begin font records, acknowledge installation,
// finish records. Value and text must be zero/empty. Font read receives raw
// UTF16LE and strict CString whitespace flags, EOF=!ReadString result.
// Font part returns snapshot: action:u32(Read0/Install1/Done2),reserved:u32=0,
// followed by decoded bytes for Install. Install outside registry admission;
// ignore Windows return/count and retain process-private font until exit.
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_font_read_v1(uint64_t,uint32_t,const uint8_t*,size_t,const uint8_t*,size_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_font_part_v1(uint64_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_style_v1(uint64_t,uint32_t,uint64_t,const uint8_t*,size_t,const uint8_t*,size_t,uint8_t*,size_t);
uint32_t XY_DOCUMENT_CDECL xy_vsfilter_document_ass_style_part_v1(uint64_t,uint32_t,uint8_t*,size_t);
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
