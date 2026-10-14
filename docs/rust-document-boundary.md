# Proposed document boundary: synchronization, lifetime and callbacks

Source review of candidate `e8f02d55825d70347c2088610796675af75a1cbe`, with
behavioral reference `4911cacb102bc0cbd41449793e54473265b04e62`. This is a
migration design proposal, not a concurrency certification or native validation.
Locations below refer to the candidate source. The inventory remains in
[rust-document-inventory.json](rust-document-inventory.json).

The first production facade should keep native host locking and callback order,
while independently controlling entry into the Rust document. A supplied
provider lock does not by itself authorize creation of `&mut Document`.
Every read as well as write must participate; returned entries, segments,
strings and styles must be copied values. Native rendering keeps its existing
outer lock across its sequence, but never a Rust loan across native work.

## Observed paths

| Path | Acquired lock and ownership evidence | Boundary consequence |
| --- | --- | --- |
| DirectVobSub file open/settings | `DirectVobSubFilter.cpp:1564` holds `m_csSubLock`; RTS receives its address at `:1598`. Settings hold it at `:1693`. | Preserve existing outer lock and publication order. |
| DirectVobSub style count/get/set | `DirectVobSubFilter.cpp:2305,2336,2380` hold `m_csFilter`; selected stream is reconstructed from `m_nSubtitleId`, then dynamically cast. Get enumerates native pairs; set validates all names, assigns sequentially, then calls `OnOptionChanged` at `:2413`. | This differs from the supplied render/pin lock. Establish a retained stream lifetime before entering the document; do not claim these lock names prove exclusivity. |
| DirectVobSub rendering | `DirectVobSubFilter.cpp:354-361` holds `m_csSubLock` around provider lookup. `SimpleSubPicProviderImpl.cpp:185-200` acquires provider `Lock` around scheduling; `:242-274` acquires it around RenderEx and animation/timing queries. | Actual render paths acquire the supplied lock, sometimes nested. Replacing it with a nonrecursive transaction lock for the entire renderer would change nesting. |
| Xy style/settings/render | `xy_sub_filter.cpp:591,630` use `m_csFilter`; RTS receives that same member at `:1605`. `RequestFrame` holds it at `:1190` across consumer-parameter lookup and provider RequestFrame (`:1210`); releases it before acquiring `m_csConsumer` and calling DeliverFrame (`:1254-1257`). Wrapper rendering acquires provider lock at `XySubRenderProviderWrapper.cpp:429`, unlocks at `:466`. | These reviewed paths share a supplied lock, but callbacks, member lifetime and all error paths still require review. Retain the release-before-DeliverFrame order. |
| Streaming | `SubtitleInputPin.cpp:627-632` holds `m_pSubLock` across helper Receive and native InvalidateSubtitle. `NewSegment` holds it at `:597`; helper clears/rebuilds before updating its time/rate state (`:145-155`, `:61-67`). DirectVobSub constructs new text pins with `&m_csSubLock` at `DirectVobSubFilter.cpp:1890`; Xy supplies `&m_csFilter` at `xy_sub_filter.cpp:2219`. | Pin lock identity is source-grounded for these construction paths. WebVTT skips clear/rebuild. End Rust access before invalidation, base calls or rebuild notification. |
| Pin lifetime | `SubtitleInputPin.h:46-51` helper retains `CComPtr<ISubStream>` while its text subclass also has a raw RTS pointer (`SubtitleInputPin.cpp:82`). CompleteConnect deletes old helper, creates and publishes new helper under lock (`:444-455`). BreakConnect/ReceiveConnection remove the stream then delete helper under lock (`:564-590`). Forwarders are `TextInputPin.cpp:33-45` and `SubtitleInputPin2.cpp:56-69`. | Keep the retained stream alive through each facade call and callback phase; a raw helper member alone is not a lifetime proof. Publication/removal may invoke host methods recursively. |
| CSRI | `csriapi.cpp:69-72,100-103` allocates an instance lock and passes it to RTS, but Open does not acquire it locally. Render calls RTS directly (`:173`) without provider Lock. Close deletes RTS, then lock, then instance (`:115-121`). RTS Render/RenderEx do not acquire the provider lock (`RTS.cpp:3493,3550,3781`). | Do not assume CSRI uses the provider-lock protocol. Define host instance lifetime and render/close coordination; retain ABI and avoid inventing a public busy result. |
| AviSynth/VirtualDub plugin | `plugins.cpp:61-64` owns `m_csSubLock` and COM provider references. Open installs a provider before parsing (`:378-385`). Render does metadata reads (`:227-230`) before provider lookup, whose implementation acquires the provider lock. Reload worker acquires `m_csSubLock` (`:282-285`). Destructor requests worker shutdown (`:98`), then members release providers in C++ destruction order. | Protect metadata facade calls too; host frame threading and publication/replacement need review. A lock in reload does not establish locking for Open or all Render reads. |

`CSubPicProviderImpl` stores the supplied pointer (`SubPicProviderImpl.cpp:32-35`)
and Lock/Unlock only forward to it (`:52-59`). It neither owns that lock nor adds
implicit locking to document access. Review nested scopes and every early return
in provider adapters rather than treating a call to Lock somewhere as a proof.

DirectVobSub destruction holds `m_csSubLock`, invalidates/releases its simple
provider, deletes pins and stops its worker (`DirectVobSubFilter.cpp:120-142`).
Xy stops its worker, deletes pins and releases its provider (`xy_sub_filter.cpp:
142-156`). Stream lists/provider COM references can extend an RTS lifetime;
member destruction order and the supplied lock's lifetime must be documented
before permitting an independent handle to outlive its native wrapper.

## Recommended coarse-call design

Keep `Document` in the safe `no_std + alloc` core. Put native synchronization,
opaque-handle validation, pinning and the small necessary unsafe access mechanism
in the existing FFI/native adapter. Do not add platform locking or callbacks to
core operations.

1. A facade operation first retains a live wrapper/handle without forming any
   Rust reference to the mutable document. The selected-stream retention must
   occur under the host lock that protects publication/removal, or through an
   independently reviewed handle registry. Reads participate just like writes.
2. Use one per-document admission mechanism in the boundary. A short native
   document access lock can serialize calls regardless of which outer host lock
   is held. Supplement it with an FFI atomic admission check **before** deriving
   any document reference, so a missed native wrapper path cannot create
   overlapping Rust loans. The check operates on stable control storage; it must
   not first construct `&mut` to a handle containing the document. Its small
   UnsafeCell/access contract belongs in one reviewed FFI module. No Rust loan
   survives the call.
3. Under admission, execute one coarse core operation and copy its outputs into
   owned snapshots. Bound copy-out lengths and release admission on every status
   and unwind path. No callback, COM call, dialog, native style conversion or
   arbitrary native allocator invocation may be hidden inside a live core loan.
   Allocator requirements already needed by the no_std bridge need review for
   reentry; a native lock alone cannot settle that contract.
4. Release Rust access and the short admission lock before native callbacks.
   Keep the existing host's outer lock at its original scope. Invoke callbacks
   synchronously at the existing point; do not defer them to a queue. Callback
   reentry starts a new operation after the previous loan ends.
5. Destruction closes admission and drains retained in-flight operations before
   dropping the owner. A raw-pointer `destroy` racing with lookup is not solved
   by an atomic inside the object being freed. Either prove the wrapper/COM host
   lifetime contract for each host, including CSRI, or use stable registered IDs
   with generation checks and pinning before lookup. Do not claim the admission
   check proves lifetime. No production switch until this choice is reviewed.

The native lock above is only for a callback-free document access. It is never
held while acquiring another host lock, waiting for a renderer, or invoking a
callback. An admission failure is an internal contract outcome requiring explicit
handling before production; do not map it to a new public HRESULT, spin forever,
or panic. Normal concurrent operations should serialize at the native boundary.
Same-thread callback reentry should find admission released. Windows traces must
establish remaining exceptional/reentrant cases before choosing their behavior.

For D2a, call `rebuild_segments`, return, then execute the existing native
`OnChanged`; it clears entry-index cache and SLA (`RTS.cpp:2012-2026`) and uses
entry count. Preserve notification even for an empty rebuild. `RemoveAllEntries`
clears entries only (`STS.cpp:4263-4266`). Do not add a callback there.
`Copy` calls virtual `Empty` first (`STS.cpp:3067-3069`), which invokes RTS Deinit
(`RTS.cpp:2006-2010`); the adapter must keep this dispatch outside core loans.
Destruction dispatch differs and must remain separate.

Render gets a segment snapshot with the original index and ordered members,
then entry/style/text snapshots. It may call GetSubtitle and parse native tags
with no owner borrow. At the existing point after successful GetSubtitle, call
`or_segment_animated(index, flag)` (`RTS.cpp:3371-3373`). Preserve native qsort
and entry-index cache identity. If callback reentry rebuilds/replaces the document
between snapshot and feedback, snapshot generation alone is not permission to
reject or retarget feedback: first record the reference's observable sequence
and specify compatible handling. This is an unresolved integration case.

D2b style get should acquire one ordered value snapshot and perform native
STSStyle conversion/output outside Rust access, retaining partial-output and
name-length error order. Style set should copy native inputs first, then validate
all names and apply sequential updates as one core operation; release before
OnOptionChanged. Preserve duplicate-name updates and null-pointer skips. The
public `SubStyle::style` remains a native C++ object pointer
(`IDirectVobSubXy.h:215-219`), never a writable Rust loan.

## Alternatives and remaining decisions

A fully enforced single native facade lock with externally proven handle lifetime
can suffice; it avoids a Rust admission primitive but makes every bypass part of
the unsafe proof. Current source inventory does not yet establish that proof.
A stable handle registry plus boundary admission gives a stronger defense against
missed facade calls and overlapping close, but adds lifetime/ordering machinery
that must be tested. An actor/worker queue or a mutex held across native callbacks
would change synchronous execution, nested lock behavior and callback timing;
neither is justified by this review.

Before integration, resolve outer-lock order for DirectVobSub style APIs versus
stream removal, CSRI instance lifetime/concurrent calls, plugin frame threading
and replacement, callback/allocator reentry, and owner lifetime relative to lock
members. Trace x86/x64 nested provider acquisition, rebuild/OnChanged, style
updates/OnOptionChanged, streaming invalidation, seek/reload and close with exact
reference provenance. This source review does not establish a race in native
code or certify a new facade; it identifies the contracts needed to avoid
introducing undefined Rust aliasing.
