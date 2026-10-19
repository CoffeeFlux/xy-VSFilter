# Rust-owned ellipse cache and exact target arithmetic

The experimental widened-outline operation no longer calls C++. The admitted
Rust owner registry now includes an ellipse cache with the last radius pair,
owned integer arc, optional intersection matrix and scalar numeric state. The
safe core drops the previous cache on radius replacement, generates the arc,
initializes numeric fields and conditionally constructs the lookup table. A
cache hit performs no arithmetic. No ellipse allocation pointer or table loan
crosses this boundary.

Safe Rust owns scratch zero initialization and row retention, sample-before-
clamp writes, the final boundary-cell overwrite, signed wrapping threshold
arithmetic, intersection clipping, and translated arc lookup. Absent matrix
cells remain absent. The native unsigned relative-Y subtraction and origin
addition wrap before the resulting index is checked. Invalid cells/indices or
impossible logical tables return an unresolved status rather than clipping or
reading uninitialized storage. Allocation failure remains unclosed.

The old scalar callback, native callback cache and function-pointer ABI parameter
are removed. Scan, region and ellipse borrows stay within one admitted operation.
The cache owns only Send data; the existing atomic gate and lifetime discipline
cover all three without adding an unsafe Sync implementation. Historical native
ellipse code and opaque-handle APIs remain in the legacy fallback, outside the
experimental renderer path. Final removal of all legacy code remains in scope.

## Numeric boundary

`windows_ellipse_numeric.rs` implements four operations per architecture:
radius-field preparation, table-parameter preparation, one table sample and an
analytic intersection. Each instance has an aligned, initialized 512-byte scalar
buffer. Assembly has fixed offsets only; it cannot address a path, arc, table,
registry or native object. Integer coordinates do not participate in addressing.

| Byte offsets | Contents |
| --- | --- |
| 0-7 | Signed integer radii |
| 8-15 | Raw f32 radius fields |
| 16-31 | Raw f64 inverse-half-radius fields |
| 128-199 | Scalar x86 spill slots |
| 256-271 | Result, y/dy/dx input words |
| 280-291 | Table ratio, squared radius and integer square input |
| 320-367 | Exact half/one/zero constants and aligned absolute-value mask |
| 480-483 | x86 square-root argument/result spill |

x86 preserves x87 operation order and the native f32/f64 spills. Radius inverses
are extended divisions stored as f64. The captured local sqrtf wrapper is
inlined as FLD, FSQRT, f32 store and reload. The only calls are scalar Windows
CRT ceil and `_ftol2_sse`, with balanced temporary stack arguments and C volatile
clobbers. Every branch leaves the x87 stack balanced. EDI addresses the local
buffer and survives those calls; no pointer is passed to them.

x64 preserves scalar SSE2 operand order, MAXSS, SQRTSS, packed conversions and
the compiler's inline ceiling sequence. Radius inverses round through DIVSS
before conversion to f64. Every used integer/XMM register is declared as an
output. There are no calls or stack effects, and the absolute-value mask is
16-byte aligned. Ordinary Rust floating expressions are not substituted for
these architecture-specific sequences.

Reference instructions, constants and binary/library provenance are archived in
[`test/compat/ellipse-numeric`](../test/compat/ellipse-numeric/provenance.json).
They come from actual cleanup native libraries compiled with MSVC 14.44.35207.
The captured reference executables each reproduce the maintained 672-call
reference trace twice. These captures are evidence for the numeric translation;
the expanded direct probe and full-renderer comparisons are separate gates.

## Validation scope

The maintained `wide_region_native` probe now has 1,536 reused-object calls:
24 straight/curved paths, 32 radius pairs and two initial floating-status states.
It includes cache hits/replacement, horizontal and empty outlines, zero-width
ellipses, thin ellipses and lookup thresholds. It compares all packed spans,
border and return values, x87/SSE status, errno and LastError twice per role.
Its Windows invocation remains documented in the widened-outline boundary.

The core tests separately check exact table query order, no work on cache hits,
lookup versus analytic selection, integer clipping before arithmetic, absent
rows, wrapping arc queries and invalid replacement. These controlled numeric
tests are not native floating-parity claims.

Finite Release/default-FP checks do not close Debug, allocator failures, other
control modes, unmasked exceptions or arbitrary host concurrency. Earlier native
ellipse FP instability and reference exit-crash evidence retain their original
attribution. The experimental flag remains opt-in. Raster output ownership,
object graphs, host adapters and vendors remain in the full migration scope.
