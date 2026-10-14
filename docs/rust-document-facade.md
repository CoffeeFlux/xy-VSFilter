# D2a entry and segment facade contract

The original caller mapping below uses candidate source captured at
`e8f02d55825d70347c2088610796675af75a1cbe`; the behavioral authority remains
`4911cacb102bc0cbd41449793e54473265b04e62`. It adds no compatibility evidence.
See [caller inventory](rust-document-inventory.json) and
[admission/lifetime proposal](rust-document-boundary.md). D2a retains native
style/default-style ownership and every metadata member, including `m_mode`;
it removes authoritative native entry and segment arrays only after all paths
below have implementations and Windows integration evidence.

## Transfer types and statuses

Use internal C++ value types `EntrySnapshot` and `SegmentSnapshot`, containing
owned strings/member arrays. They are neither persistent mirrors nor aliases
into the document. An entry contains text, Unicode flag, style, actor, effect,
left/top/right/bottom margins, layer, start/end and readorder. A segment contains
start/end, animated and ordered entry indexes. Preserve indexes exactly; copied
stale segment members remain representable after entry removal.

The FFI uses explicit-width scalars (`u16` code units, `i32` signed native
indexes/readorder/layer/margins, `i64` times), length-delimited strings and arrays,
and explicit success/rejection/refusal statuses. Never transfer CString,
CAtlArray, CRect, bool layout, usize layout or STSStyle as Rust-layout structs.
Check lengths, multiplication and index representability before copying. The
prepared record may contain empty text after native preparation, embedded NULs
and unpaired surrogates; do not reject or normalize them at transfer.
CString style/actor/effect adapters must establish the actual native character
configuration and comparison behavior. The current core uses raw `u16` names. Windows observations now demonstrate
that full-vector equality differs from CString for `A\0B` versus `A`; its current
rename contract therefore remains restricted to NUL-free names.

Snapshot acquisition creates one owned result under document admission. Size
query and copy-out then address that immutable result rather than rereading a
possibly changed document. Release it explicitly after native materialization.
All capacity failures return required lengths without partial out-of-bounds
copying. No returned pointer aliases canonical state. One reviewed copy helper
serves strings and members; do not introduce per-field raw native loans. Native
invalid-index/exception behavior and allocation-failure partial mutation need
an adapter contract; a generic new HRESULT or panic is not compatible evidence.
Core Refusal variants are unresolved integration outcomes, not native acceptance
or rejection results. Never fall back to a persistent CAtlArray owner on refusal.

## Required operations

Names are proposed internal operations; existing public DLL signatures remain
fixed. A core mutation produces values/status only. Native orchestration invokes
callbacks after Rust access ends, preserving its existing outer lock scope.

| Operation | Exact storage effect and native sequencing | Current safe core |
| --- | --- | --- |
| `create`, `destroy` | One owner per CSimpleTextSubtitle instance; drop only after lifetime admission closes. Native RTS Deinit/destructor dispatch remains outside the core. | Default/Drop storage exists; FFI lifetime contract needed. |
| `entry_count`, `segment_count`, `is_empty` | Observe respective arrays independently; empty entries do not imply empty segments. | Slices/is_empty available internally. |
| `entry_snapshot(index)`, `segment_snapshot(index)` | Owned complete record; segment invalid index yields absent for GetSegment. Entry invalid-index semantics must follow originating method. No mutation/notification. | Need bounded indexed snapshot API and adapter. |
| `append_prepared(record, mode)` | Native FastTrim/rejection/WebVTT transformations/CR and LF replacement/style preparation occur first. Core supplies default readorder from current entry count, then Add/EntryOnly/WebVTT insertion. EntryOnly admits zero duration; Add rejects it. Dropped Add does not advance entry count. No OnChanged. | `add_prepared`; prepared-suffix evidence only. |
| `clear_entries` | Clears entries while retaining every segment, animated flag and cache. No notification. | `remove_all_entries`. |
| `clear_storage` | Clears both arrays only; native Empty separately resets screen size/frees styles, with existing virtual dispatch and order. No invented notification. | `clear_entries_and_segments`. |
| `rebuild_segments` | Replace segments from current entries in storage order, reset animation; return before native OnChanged then LogSegments. Callback still executes on empty rebuild. | `rebuild_segments`; callback/log adapter needed. |
| `or_animation(segment, flag)` | OR into exactly selected segment after successful GetSubtitle at RTS.cpp:3371-3373. False cannot clear it. New split segments/reset rebuild follow native rules. | `feedback_animation`; stale identity/reentry contract unresolved. |
| `correct_last_end(bound)` | Set last stored entry end to min(old,bound), even below start; change no segments or notification. Caller checks IsEmpty as native MicroDVD does. | `correct_last_end`. |
| `replace_entry_style(index, units)` | Replace only style string; no segment rebuild/notification. Supports unknown-style loop interleaved with dialogs. | Missing. |
| `rename_entry_styles(old,new)` | Rewrite matching entries in storage order when native AddStyle renames an old map value, before inserting new style. Preserve native string comparison. No rebuild. | `rename_style` for established raw-unit comparison domain only. |
| `replace_text(index, prepared_units, optional_unicode_flag)` | Store already converted text; retain existing flag for SetStr, replace flag only for ConvertUnicode. No other entry change or notification. | `set_prepared_text`; conversion and charset preparation remain native. |
| `copy_storage_from(other)` | Deep-copy entries and segments including animated flags. Native Copy first calls virtual Empty, assigns its explicit metadata subset, CopyStyles (which may rename entries), then copies segments, then entries. Source=self is read after Empty, so cannot preclone self. | Clone exists; ordered native lifecycle/copy failure seams missing. |
| `reorder_entries(permutation)` | Apply a complete validated new-index-to-old-index permutation once; preserve record fields/readorder. Segment indexes remain unchanged until existing rebuild point. No stable-sort substitution or implicit notification. | `reorder_entries` validates and moves the supplied permutation then rebuilds; exact native-compatible sorting model remains missing. |
| `replace_entry_time(index, endpoint, value)` | Change only the chosen start/end scalar. Used in native conversion order; no clamp, repair or implicit rebuild. | Missing. |
| `query_time(kind,time)` | Return exact selected index, found flag and selected array count; no entry dereference for segment members. Empty Next/Containing gives index 0/false, empty Entry -1/false. | Existing TIME-only query. |
| `raw_time(kind,index,endpoint)` | Return raw entry/segment endpoint or original invalid-index sentinel -1; no conversion or mutation. | Missing bounded operation. |
| `query_translated(kind,time,mode,fps)` and `translate_*` | Preserve native search's requested translation sequence and the distinct TranslateSegmentStartEnd mode behavior; native numeric adapter until exact model exists. Rust access must end before invoking native numeric services. | Pull search helper exists; FRAME/invalid-mode owner integration missing. |

`copy_storage_from` cannot be reported as faithful native Copy until exception
and partial-copy effects are covered. If preserving native segment-then-entry
partial failure requires staged operations, expose two bounded copy commits
rather than falsely promising atomic whole-copy behavior. Successful calls
must never retain pointers into either source owner.

A conversion orchestration reads raw start, computes native llround, commits
start, then reads/computes/commits end, entry by entry (`STS.cpp:3432-3458`). It
then writes native `m_mode` and calls the existing rebuild/OnChanged sequence.
Preserve no-op when already in target mode, and initialized-state/error order.
The endpoint operations above make that sequence possible without mutable
loans or a persistent native array. Batching is allowed only after evidence
establishes equal observation and partial-failure behavior.

Sorting remains a requirement, not an excuse to retain a native owner.
`STS.cpp:3788-3804` uses native qsort with separate comparators; renderer qsort
at `RTS.cpp:3316-3360` is a distinct native render decision. A permutation
operation is the necessary commit mechanism, but obtaining its exact sequence
requires a pinned CRT-compatible safe sorting model and comparator arithmetic
evidence. Temporarily sorting copied keys in native code is a disclosed adapter
for development only; it does not close the document sorting requirement or
justify claiming D2a integrated complete.

## Caller edits required before array removal

| Direct source access | Facade replacement |
| --- | --- |
| STS lifecycle Copy/Empty (`STS.cpp:3065-3129`), IsEmpty/RemoveAllEntries (`:4258-4266`) | lifecycle orchestration + storage copy/clear/count |
| Add/EntryOnly (`:3137-3306`) | native preparation then append_prepared; remove SegmentInsertion reseeding and native Apply |
| MicroDVD (`:1756-1757`) | correct_last_end after is_empty |
| Unknown-style loop (`:3321-3357`) | entry_count and per-entry snapshot/replace_entry_style; preserve last-style heuristic and prompt sequence |
| AddStyle rename loop (`:3403-3408`) | rename_entry_styles with reviewed CString comparison; native map retains ownership |
| GetStyle/GetCharSet (`:3687-3718`) | entry_snapshot style key, then existing native forced/default/map lookup |
| text reads/edits (`:3720-3784`) | entry_snapshot + native conversion + replace_text; SetStr does **not** assign fUnicode |
| Search/Translate (`:3461-3685`, `STS.h:278`) | query and raw-time/snapshot operations; replace borrowed SearchSubs/GetSegment return contracts internally |
| Sort/CreateSegments (`:3801-3887`) | exact permutation then rebuild; remove Segments reseeding and native copyback; callback/log operate on snapshots |
| Open count/error handling (`:3939`), SaveAs (`:4121,4142-4144`) | entry_count/snapshot; keep parser/serialization logic, file effects and metadata native |
| RTS cache sizing (`RTS.cpp:2023`) | entry_count; preserve existing OnChanged point |
| RTS effect/margins (`:3137,3190`) | entry_snapshot (effect and margins); no persistent native mirror |
| RTS scheduling/IsAnimated (`:3265-3310`) | index/count/segment snapshots, raw translations and animation getter; keep POSITION=index+1 |
| RTS ParseScript (`:3329-3373`) | selected segment snapshot; entry snapshots for raw end eviction/layer/readorder/times; explicit animation OR; SLA receives copied ordered members |
| STS diagnostics LogSegments (`:3886`) | temporary complete segment snapshot only at existing log point |

SubtitleInputPin (`:151-152,188-283,535`), USF (`USFSubtitles.cpp:250-384`),
CSRI/plugins Open, and CCDecoder Add/Sort/SaveAs keep their public/native calls;
those CSimpleTextSubtitle methods route to the facade. Both filter style APIs
continue through native STSStyle ownership in D2a. They still participate in
owner admission whenever an operation touches entry style names. Every metadata
read/write remains inventoried for D2b. No caller is removed from scope because
static reachability is uncertain.

## Next smallest real integration cut

Implement internal value-returning EntrySnapshot/SegmentSnapshot/count accessors
and explicit animation feedback in CSimpleTextSubtitle with a native backend
first, then route **all RTS direct array reads/writes** through them in one
reviewable change. Replace SearchSubs2's mutable view in ParseScript with an
owned segment result, maintain index-based cache/SLA/qsort behavior, and feed
animation back at the same source point. This changes a production consumer
boundary with native backing and is not a Rust ownership claim. It directly
removes the longest native mutable segment loan before owner switching.

Next wire a single persistent Rust owner behind the same internal surface under
an opt-in build, implementing missing edit/copy/reorder/time operations and
routing STS's remaining mutations. Native arrays must then be compiled out for
that build, rather than maintained beside the handle. Retire transient
segments/segment_insert/timeline_search reseeding crossings as each owner call
supersedes them. Do not enable the owner switch until all listed callers compile,
refusal/allocation/index/encoding contracts are reviewed, admission and lifetime
are enforced, and exact-commit Windows x86/x64 rendering/streaming checks pass.

Snapshot validity across native callback reentry, source/destination copy
admission, close versus live operations, numeric exceptions and CString
embedded-NUL comparisons remain explicit blockers to claiming compatibility for
the corresponding paths. Generation checks can detect invalidation; their
failure policy is still a behavioral decision requiring reference evidence.

## Caller-isolation implementation status

The current change implements the native-backed renderer boundary. RTS calls
`GetEntryCount`, owned `GetEntrySnapshot`/`GetEntryEffect`, scalar end/layer/order
accessors, `FindNextSegment`, `GetSegmentEntryCount`, `IsSegmentAnimated`,
`SearchSubs2Snapshot` and `OrSegmentAnimated`. All direct entry/segment array
accesses and borrowed segment search calls in RTS are removed. Scheduling uses
scalar results without allocating member copies. ParseScript owns its selected
segment members and sends animation OR feedback at the existing point after
successful GetSubtitle. Public exports, native storage and CRT qsort remain.

Source review found no explicit document or host callback in the reviewed
GetSubtitle path between membership capture and animation feedback. This does
not establish equivalence under allocator reentry, concurrent mutation or
allocation failure: copying segment members adds an allocation before cache
cleanup. Windows focused ATL checks and integrated renderer comparisons remain
required. This cut isolates a production consumer; it transfers no canonical
storage to Rust and introduces no Rust FFI or unsafe code.

The core now also accepts prepared text edits, validated permutations and bulk
already-converted endpoint pairs. The latter is a bounded successful-operation
handoff with rebuild; per-endpoint partial failures and native mode publication
still require adapter work. The differential harness compares native full Sort
and conversion state with candidate replay using only native control decisions
(permutations/rounded scalars), explicitly retaining the CRT computations.
