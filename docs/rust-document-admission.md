# Native admission slice for the persistent document owner

Reviewed candidate `d2b75237af64861dab2152ddb183af08c0a9dbbf`; reference remains
`4911cacb102bc0cbd41449793e54473265b04e62`. This proposal does not enable a
production switch or establish exceptional allocator/host behavior.

Implement one module-global native `CCritSec` gate around every private document
C ABI call. Keep the Rust registry's nonblocking admission unchanged. Normal
parallel callers wait in a native critical section; they do not poll BUSY. A
recursive native gate permits same-thread calls once a prior Rust loan has ended.
The registry remains the authority that refuses overlapping Rust access. The
native gate must never stay held across an entire renderer/parser operation.

`CCritSec` already uses Initialize/Enter/Leave/DeleteCriticalSection and tracks
nested ownership in debug builds (`src/filters/BaseClasses/wxutil.cpp:948-996`;
release definitions in `wxutil.h`). The project targets Windows 7 and static CRT
(`src/common.props:27,58,73`). Reuse that existing primitive and platform surface;
do not add WaitOnAddress, Rust std synchronization, or a spin loop. Construct the
gate as a native module object before document calls begin. Do not initialize or
wait for document operations from DllMain, or rely on Rust static destructors to
release owners at DLL detach. Review native module destruction ordering with the
new wrapper before actual integration.

## Exact native wrapper responsibilities

Add one noncopyable internal `DocumentOwner` member to CSimpleTextSubtitle in the
opt-in Rust-storage build. It stores a u64 ID and has value-returning methods
matching the existing isolated facade. Its private `Invoke` acquires the global
gate, calls exactly one C function and releases the gate on return. All document
and snapshot create/read/drop/copy calls use Invoke, including destruction and
error cleanup. No other production code calls the raw private exports.

Prepare input buffers and native conversions before Invoke; decode/materialize
native values after Invoke. Keep existing host outer locks where callers already
hold them. The only new order is outer host lock -> gate -> Rust admission. While
the gate is held, do not acquire another host lock or call virtual notification,
UI, COM, parser/render code or another wrapper method. Cross-document copy is one
C ABI call and one registry acquisition, never two owner locks.

Native entry-count/end/layer/readorder/effect accessors must use this same gate.
Do not treat scalar reads as exceptions. Preserve the already implemented
FindNextSegment/GetSegmentEntryCount scheduling behavior without allocating full
member snapshots merely to test existence. Existing fixed-size count, entry-header,
segment-header and TIME-query outputs already provide these scalar values; reuse
those exports rather than adding one crossing per field. FRAME/invalid-mode
search orchestration must retain native translation order using raw header times
until its corresponding owner operation is established.

A snapshot is a separate RAII reply ID. Open it through Invoke, release the gate,
query its immutable size, allocate the native output buffer outside the gate,
then copy bytes through Invoke. Allocate CString/CAtlArray output and decode
outside admission. Drop the reply ID through Invoke on every exit. The reply
can survive owner destruction and intervening document changes. The current
snapshot-open/read functions still allocate staging bytes inside Rust admission
(`rust/ffi/src/document.rs`); moving native output allocation outside the gate
does not eliminate that allocator contract.

An owning wrapper must remain alive through the native operation and its
post-call materialization. COM paths should retain a CComPtr selected under the
host lock protecting stream publication/removal, rather than trusting a raw
pointer reconstructed from selection state. Pin helpers already retain their
stream (`SubtitleInputPin.h:46-51`); preserve that retention across callbacks and
removal. Plugin provider fields are CComPtr (`plugins.cpp:62-64`); obtain a local
retained provider under its actual publication lock before accessing the owner.
DirectVobSub style get/set currently select via m_nSubtitleId under m_csFilter
while rendering/pin paths use m_csSubLock (`DirectVobSubFilter.cpp:2336,2380,1598`).
Choose and document the stream-selection retention point before modifying that
path. The gate serializes Rust IDs; it cannot validate a C++ object already freed
before entry.

CSRI currently calls Render directly and deletes the RTS, then its lock and
instance (`csriapi.cpp:115-121,149-176`). A valid host instance lifetime remains
required by the public pointer ABI. If concurrent render/close is to be supported,
implement native instance admission before dereferencing the instance: a stable
lookup table pins active instances, close removes admission and delays deletion
until existing pins finish. Use one defined close policy established against the
reference host contract; do not wait while holding a lock needed by those calls.
An arbitrary call with an already retired/reused public pointer is a separate
ABI lifetime question, not solved by the Rust ID registry or by AddRef after
unsafe pointer selection. This does not block testing the internal owner under
retained live RTS instances.

## Allocator reentry decision

`rust/ffi/src/runtime.rs:6-25` uses _aligned_malloc/_aligned_free. Infallible
allocation failure already aborts (`:43-47`); compatible recoverable native OOM
is not established. Microsoft documents that [_aligned_malloc is based on
malloc](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc?view=msvc-170),
and [_set_new_mode can enable a new-handler for malloc failures](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/set-new-mode?view=msvc-170).
Repository search found no _set_new_mode/_set_new_handler/Newmode.obj setup.
That search does not prove the pinned static CRT configuration or host behavior.

Same-thread allocator recursion while a Rust loan is live acquires the recursive
native gate and immediately receives private BUSY from Rust. It must not wait,
retry, return a newly invented HRESULT, or be counted as parity. A nonrecursive
gate would deadlock before that explicit outcome; allowing a second Rust borrow
would be unsound. Therefore the first gated adapter remains opt-in and records
this internal contract failure as unresolved. Ordinary successful allocations
and normal parallel calls can be integrated and compared now; a universal
production switch cannot be justified until this exceptional path is resolved.
Do not silently change _set_new_mode globally or replace the allocator to make
this condition disappear: either changes observable native failure behavior.

The narrow Windows probe needed here is synthetic: with the pinned static CRT,
record _query_new_mode and handler configuration; install a controlled temporary
handler, arrange a bounded allocator-failure seam, and attempt nested private
admission. Verify immediate BUSY/no second loan and restoration of CRT state.
Run in a fresh helper process, never replay an excluded crashing reference
history. This establishes the mechanism and scope, not native OOM parity.
A reference-supported new-handler that reenters the same document successfully
would require a separate staged-operation/reentry design; the present registry
cannot emulate that behavior by choosing a different lock.

## Implementable next slice and checks

Build the native DocumentOwner/Reply RAII adapter behind a test-only option,
reuse fixed-size scalar/header replies, and drive it with the existing prepared
lifecycle replay. Keep native arrays authoritative in the production build.
Exercise two live owners and copied snapshots from parallel threads, owner close
with retained reply reads, one-call copy, and notification reentry **after**
Invoke returns. Verify no BUSY in normal serialized calls and record the
synthetic nested-admission BUSY separately. Check unchanged public DLL exports,
Win7 import/dependency surface and both Windows architectures.

Then route the isolated STS/RTS entry/segment surface to the persistent owner and
compile out authoritative arrays in the opt-in build. This requires the already
listed native preparation, Copy/Empty, edit/style rename, sorting and conversion
orchestration, not another standalone algorithm leaf. Keep unresolved allocator,
wrapper-lifetime and exceptional partial-failure outcomes visible; successful
normal-path parity does not close them.

## Isolated probe delivered

`tools/compat/document_admission.h` and `.cpp` provide the test-only gate and a
standalone synthetic executable linked to the current Rust FFI static library.
Windows uses actual CRITICAL_SECTION; other platforms use recursive_mutex only
for local mechanism checks. No renderer/reference history is executed.

The probe checks two-thread calls on separate owners, one-call storage copy,
destroy queued behind an in-flight native operation, stale lookup after destroy,
immutable reply reads after owner close, and notification-style reentry after
admission release. Native worker rendezvous use five-second bounded waits; the
runner must also apply a process timeout (20 seconds used locally). A synthetic
rendezvous deliberately holds the native gate before entering Rust. The destroyer
then asserts that a test-only TryEnterCriticalSection/recursive_mutex.try_lock
attempt fails on its own thread before signaling readiness and making the
blocking destroy call. This establishes observed contention without sleeps or
relying on scheduler timing. This instrumentation is not a production callback or a
wait inside a Rust loan. All threads join before gate destruction.

A local macOS build linked `target/release/libxy_vsfilter_ffi.a` with C++17 and
AddressSanitizer/UndefinedBehaviorSanitizer; all three probe groups passed with
normal_busy=0. That used the portable recursive-mutex backend and does not
validate Windows critical sections, no_std/static CRT allocation, COM/CSRI
wrapper lifetime or same-thread allocator reentry. The existing Rust registry
unit test separately exercises Busy with an actual retained registry loan.

To integrate the probe into `document_check.py`, reuse the FFI build's static
library, target architecture and native-static-libs. Compile the new `.cpp`
with `/MT /EHsc /std:c++17` on Windows and existing sanitizer flags elsewhere;
run it as a separate bounded process, require successful exit and exact backend
marker, and archive compile command/binary hashes/stdout. Do not translate its
private statuses into public host behavior. The files add no production gate.
