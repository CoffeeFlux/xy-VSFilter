# Integer scan-line migration

The experimental renderer delegates the ordered line sampler in
`ScanLineData::_EvaluateLine<flag>` to safe Rust. `scan_line::Line` computes the
half-sample prestep and fixed-point slope, then yields row/position pairs.
`edge_storage::Storage::line` validates the complete row/index range, doubles
capacity with the reference's policy, and inserts every edge into its existing
Rust-owned storage. C++ receives only the updated next-edge and capacity scalars.

This replaces the native loop's unchecked row indexing and signed arithmetic.
The x-coordinate difference wraps in **32 bits before** widening and shifting;
the accumulator, slope product and packed position preserve the observed machine
wraparound explicitly. All arithmetic is integer, so existing floating status
is untouched. Curves still call this line sampler through their native evaluator.

The private FFI export borrows the existing opaque edge owner exclusively. It
accepts scalar coordinates and an eight-byte output span, validates nulls and
flag values, and writes a detached little-endian result only after success. No
path pointer, row pointer, edge slice or iterator crosses the ABI. The owner
pointer has the same live/exclusive lifetime contract as the existing edge API;
migrating this older pointer-based API to checked IDs remains in scope.

Negative/out-of-range rows, wrapped edge counts and non-progressing capacity
growth are unresolved native memory-access/hang outcomes. The Rust operation
rejects them before changing edges; it does not clip or report them as compatible.
Allocation failure is a separate error. Experimental scan initialization no
longer falls back to native raw-buffer allocation when Rust allocation fails.
That failure path is not claimed as reference parity.

`tools/compat/scan_lines_native.py` compares the actual cleanup and candidate
`ScanConvert` implementations, twice per role, using the cleanup reference
`67da07cda068088b93e6de300f56cbad15ab6c5d` and Win32 `/arch:IA32` or x64 SSE2.
Its 384 histories cover 192 deterministic paths with straight lines, native
Bezier/B-spline callers, multiple figures, ignored types, close flags, horizontal
paths, empty paths on a reused object, integer X limits, and capacity growth.
The observer records every packed span, size, next-edge/capacity counter,
first/last point, Boolean result, floating status, errno and LastError. All
reference row accesses in this success fixture stay within the allocation.

Example from the matching Visual Studio developer shell:

```text
python -X utf8 tools/compat/scan_lines_native.py --reference-root artifacts/cleanup-reference --platform Win32 --output artifacts/scan-lines-x86
python -X utf8 tools/compat/scan_lines_native.py --reference-root artifacts/cleanup-reference --platform x64 --output artifacts/scan-lines-x64
```

This is one part of the full scan conversion migration. Native path dispatch,
figure connection/closure, Bezier coefficient/step arithmetic, scan result object
ownership and the remaining host/vendor implementation still need porting. The
experimental switch remains opt-in, and earlier unresolved reference outcomes
remain recorded separately from successful comparisons.
