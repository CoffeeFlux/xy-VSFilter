# Rust-owned widened outlines

This audit records implementation `f72fa4ba`. Its temporary ellipse callback is
superseded by the [Rust ellipse owner](rust-ellipse-owner-boundary.md). The
current maintained observer has 1,536 calls and 32 radius pairs; the 672-call
counts below retain their original checkpoint scope.

Under the experimental renderer flag, `ScanLineData2` keeps a checked Rust
region ID instead of a persistent C++ span vector and border scalar. Safe Rust
owns radius clamping, the two horizontal merges, ordered linked-span insertion
and removal, arc queues, deadlines, row emission and persistent output. The
source scan and destination region are admitted together through disjoint fields
of the existing registry. No source snapshot or span pointer enters widening.
Rasterization reads a detached packet containing the border and packed spans.

The sweep preserves cleanup order: sentinel back/front intersection queries,
left before right updates, insertion before the current node while retaining
that node as the cursor, per-call span flushing, and removal when the left queue
becomes empty. It does not sort or normalize spans. The native packing macro
sign-extends a negative X through the upper word; Rust preserves that OR. The
existing safe fixed-slot queue retains stale slots and tail wrap behavior.

## Transitional arithmetic boundary

Ellipse initialization, cached radii, intersection tables and scalar floating
calculations remain native for this checkpoint. A synchronous callback accepts
only copied integers: initialize, left/right intersection, left/right arc query.
It receives four temporary writable output bytes and retains no Rust address.
No list, span, queue or region owner is passed to it. C++ catches exceptions and
returns a status before control reenters Rust. Its cache is serialized by the
existing native gate; the Rust atomic admission refuses reentrant registry
access, including destruction. The registries contain only owned Send values.

This callback is explicitly transitional. The final endpoint includes moving
the cached ellipse state and target-exact floating calculations into Rust and
removing the callback, old opaque ellipse handles and remaining native fallback.
No claim is made that this completes the renderer or the whole library.

Five private region exports create/destroy IDs, convert from a scan ID, query
snapshot length and read a detached packet. IDs are monotonic and never reused.
The packet is a little-endian i32 border, u64 count and pairs of u64 endpoints.
Invalid IDs, null callbacks and short output capacities leave caller output
unchanged. Failed operations may retain partial internal state. No native raw
span/list allocation fallback is used by the experimental sweep.

## Validation and limits

`tools/compat/wide_region_native.py` calls the actual cleanup and candidate
`CreateWidenedRegion` methods twice per role. Its 672 calls reuse objects across
24 straight/curved paths and 14 radius pairs, including negative clamping, zero
axes, two-pass horizontal widening, empty/reset, lookup thresholds and unequal
axes. Every packed output span, border, return value, x87/SSE status, errno and
LastError is compared. The reference is cleanup commit
`67da07cda068088b93e6de300f56cbad15ab6c5d`. Run from a VS developer shell:

```
python tools/compat/wide_region_native.py --reference-root artifacts/cleanup-reference --platform Win32 --output artifacts/wide-probe-x86
python tools/compat/wide_region_native.py --reference-root artifacts/cleanup-reference --platform x64 --output artifacts/wide-probe-x64
```

These are finite Release/default-FP observations with initialized bounded paths.
Native uninitialized table rows, invalid span/list shapes, allocation failure,
Debug and arbitrary concurrent host calls remain unclosed. The known native
leftover-A horizontal merge hang is refused as unresolved, never a parity pass.
The earlier reference exit crash and optimized ellipse FP instability retain
their original evidence; a later passing run cannot erase them. Production
defaults remain unchanged. Windows DLL, COM and full rendering regressions are
recorded separately after the implementation is committed and frozen.
