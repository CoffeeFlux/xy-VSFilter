# Owned BeBlur orchestration

The experimental `Rasterizer::BeBlur` consumes a checked overlay ID and creates
an independent Rust image. Safe Rust expands the geometry with legacy wrapping
metadata, allocates initialized body/border planes, copies border before body,
selects the filtered plane, runs the fixed 3x3 passes, and applies fractional
horizontal/vertical filters with the intervening transposes. No image pointer or
column-history loan crosses the new boundary. The native facade materializes
detached snapshots for the remaining clip/draw consumers.

Logical source bytes carry their initializedness. Uncopied bytes start at zero;
unknown source padding is never read. Unknown logical bytes can survive a copy,
but an attempt to filter them returns unresolved status 6. Negative/overflowing
allocation geometry and invalid copy ranges also report unresolved. This is not
a compatibility claim for native corruption or partial allocation failure.

The numeric backend reproduces selected instructions from the linked cleanup
reference, independently for Windows x86 and x64. Assembly addresses only fixed
slots in its own initialized, aligned 256-byte scalar buffer. x86 uses balanced
x87 stacks and the CRT's scalar `_ftol2_sse`/`_ftol2` conversions; x64 uses the
captured SSE conversion and spill order. The safe core calls geometry before
copying, scaling after copying, fraction calculation after integer passes, and
each axis's coefficient calculation after that axis's integer passes. Constants
and raw float arguments are stored as bits. Rust float-to-int casts and f32
expression reassociation are not substitutes for this backend.

Selected reference disassembly and original capture provenance are archived in
`test/compat/be-blur-numeric`. The capture observer remains the earlier unchanged
overlay observer; it is not relabeled as the expanded current parity run. The
maintained overlay observer now exercises 12,804 calls: eight paths, four radii,
ten settings (including rounding boundaries, zero and very small strengths),
raster/BeBlur/Gaussian/combined stages, copy/offset changes, chained subpixel
shifts and empty sources. It compares metadata, every known pixel/padding byte,
FP/error status and allocation deltas twice per role. Gaussian unknown padding
is excluded explicitly. Windows x86/x64 comparisons are required for validation.

The checked registry admission protects source lifetime through the operation.
The result is inserted only after success; ID output is a detached eight-byte
copy and supports unaligned storage. Stale IDs/null output are rejected. The
source owner is never mutated. Native snapshot allocation accounting remains
unchanged; extra canonical Rust storage is not charged to the legacy cache.

The unused native temporary `unsigned[]` allocation is omitted, as are borrowed
column buffers and native transpose scratch. Rust owns fallibly allocated
scratch instead. Allocation failure equivalence, Debug, nondefault FP controls,
unmasked exceptions and arbitrary host concurrency remain unclosed. Gaussian
orchestration/cache, clipping, drawing, native object graphs, host adapters and
vendors remain in scope. The experimental switch remains opt-in.
