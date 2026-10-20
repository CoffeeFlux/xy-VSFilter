# Owned Gaussian filtering boundary

The behavioral reference is CoffeeFlux cleanup `67da07cda068088b93e6de300f56cbad15ab6c5d`.
This checkpoint moves the experimental Gaussian image operation into safe Rust:
byte conversion, all fixed and v6 filter traversals, both transposes, scratch,
output geometry and initialized-byte masks. Coefficient construction was moved
in the preceding checkpoint. The native coefficient weak cache and sigma products
remain transitional, as do the combined Blur dispatch and downstream consumers.

`gaussian_filter` represents floats as binary32 words. Four-lane arithmetic,
comparison, x86 load/spill and the two final conversion policies use fixed local
slots in an audited Windows backend. No image pointer, index, allocation or loop
crosses that numeric boundary. SIMD conversion saturates and uses MXCSR rounding;
scalar tails add binary64 0.5, truncate and retain the low byte. The x86 tail calls
the same scalar `_ftol2` conversion as cleanup. Accumulation uses coefficient
order 0,2,1,3; fixed-tail, symmetric-three and v6 paths are retained.

The checked overlay entry accepts an owner ID and a detached initialized kernel
packet, then returns a new ID. Input borrowing ends before any output write.
The same owner gate excludes release/mutation. Detached native snapshots remain
for downstream consumers. Known-byte masks preserve Gaussian's unwritten padding;
the unfiltered body has fully initialized top/bottom rows and logical middle rows.
Unknown logical filter input and impossible geometry are refused as unresolved,
not sanitized or counted as reference matches.

`test/compat/gaussian-filter-numeric` archives linked reference disassembly and
its original provenance. The direct filter observer compares every output word,
including prefixes and untouched padding, plus FP flags, errno and LastError.
Its admitted coefficients and source values are finite (including subnormals,
negative values and signed zero); arbitrary NaN payloads, overlapping coefficient
and image arrays, nondefault FP controls and unmasked exceptions remain unclosed.
The actual overlay observer additionally covers Gaussian strengths through 32,
anisotropic scaling, copies and chained subpixel shifts.

Windows x86/x64 comparison evidence is recorded in [the local evidence](rust-gaussian-filter-local-evidence.json): 3,840 direct filter cases, 17,924 actual overlay calls, all preceding focused probes, and the full 1,222-case / 13,186-frame rendering corpus match per architecture. Rust passes 379 core and 142 FFI tests per architecture; Python passes 212 tests with one skip. See the migration handoff for remaining scope and the isolated COM environment failure/retry.
The experimental production switch remains opt-in; this is not whole-library closure.
