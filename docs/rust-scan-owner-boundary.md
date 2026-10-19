# Rust-owned scan conversion

The experimental `ScanLineData` owns a checked Rust scan ID. It no longer owns
native first/last-point state, edge buffers, row heads, an edge-owner pointer or
a persistent native span vector. `ScanConvert` passes its scan ID, the existing
path ID and dimensions into one admitted Rust operation. Native rasterization
and widening consumers receive detached snapshots of the result.

`scan_convert::Scan` owns dimensions, figure state, counters and final spans.
Temporary edges live in `edge_storage::Storage` and are dropped after span
collection. Safe core code handles type dispatch, ignored move-NC/unknown types,
figure bridges and closure, truncated curves, sample iteration, line insertion,
row collection and packed-span output. It preserves the native behavior of
masking CLOSEFIGURE rather than directly closing a path on that flag. Empty
input clears spans and dimensions while retaining previous figure/counter state.

The path and scan registries share one existing atomic admission. Their owned
values contain only Send data. HRTB closures prevent references escaping; a scan
operation borrows the path and scan from disjoint registry fields. It does not
nest registry admissions or call back into C++ while holding a path loan. GDI
path operations continue to use the same gate. Five private scan exports create,
destroy, convert, query length and read a detached packet. IDs are monotonic and
never reused. Native serialization holds its outer gate across length/read.
Short reads and invalid IDs leave caller output unchanged.

## Curve arithmetic

`windows_scan_curve.rs` preserves cleanup MSVC's target instructions. Reference
disassemblies and binary/library provenance are retained in
[`test/compat/scan-curve`](../test/compat/scan-curve/provenance.json). On x86, the
first sample retains extended coefficients; later samples reload f64 spills.
Each t update spills to f64 but retains its extended result for the next sample
and comparison. Origin conversion is conditional and X-before-Y; sample and
endpoint conversion are Y-before-X. The x64 backend preserves SSE2 operand order,
packed absolute-value masking, SQRTPD and scalar comparisons/conversions.

Every backend instance is a 16-byte-aligned, zero-initialized `[u64; 96]`.
Assembly receives only that local array's address. All accesses use fixed offsets
inside its 768 bytes; no index, point coordinate or foreign address participates
in addressing. Four initialized points enter as copied integer bits.

| Byte offsets | Use |
| --- | --- |
| 0–255 | Native scalar spill slots (x86 uses only 0–127) |
| 256–345 | Six raw x87 80-bit values, or x64 scalar register state through 335 |
| 432–439 | Conditionally converted initial origin |
| 440–447 | Current or final integer point |
| 448–455 | Continue flag, initially zero |
| 512–543 | Four input points |
| 552–559 | Spline and first-set flags |
| 640–687 | Exact reference constant bits |
| 704–719 | Aligned x64 two-lane absolute-value mask |

All eight assembly blocks return with balanced x87 stacks and declared clobbers.
x86 preparation saves six retained values and point evaluation consumes them;
advance either saves the six values for continuation or discards them on exit.
The only calls are the x86 CRT `_ftol2_sse` scalar conversion. EDI survives those
calls and C volatile clobbers are declared. x64 has no calls or stack effects;
the aligned mask load and XMM register outputs are explicit. Coefficients and
step values derive only from i32 points; no arbitrary floating input enters this
backend. Safe Rust controls iteration and owns all edges and spans.

## Limits and validation

Invalid negative point bases, out-of-range rows, impossible counts and packed
span overflows are unresolved reference outcomes, not clipped/normalized paths.
The boundary refuses them. Allocation failure is separate and has no native
raw-buffer fallback. Failed conversions may retain partial internal state; no
claim is made that these unresolved paths match native memory corruption.
Fresh native scan state was uninitialized before its first nonempty conversion;
Rust initializes it safely, and comparisons do not claim those private
uninitialized values match. Other FP control modes, unmasked exceptions, Debug
behavior and arbitrary host concurrency remain unclosed.

The maintained `scan_lines_native` observer now reads the Rust snapshot for the
candidate and actual native private state for the reference, preserving the same
input generator and trace. Its 384 histories compare every packed span, counter,
size, first/last point, return value, floating status, errno and LastError, twice
per role on Windows x86/x64. Reproduction commands are in the
[scan-line boundary document](rust-scan-lines-boundary.md).

This retires the older opaque edge-owner API from the experimental scan path;
that API remains for the legacy fallback and its historical tests. Widened-region
ownership, raster output buffers, caches/object graphs, host adapters and vendors
remain part of the full migration. No production default is switched here.
