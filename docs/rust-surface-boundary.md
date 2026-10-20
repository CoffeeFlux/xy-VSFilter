# Host surface output operations

Reference: CoffeeFlux cleanup `67da07cda068088b93e6de300f56cbad15ab6c5d`.
The experimental path moves packed and planar XyBitmap alpha blits, packed
copies, external alpha inversion and Rasterizer solid-fill orchestration into
the safe Rust surface module. The existing host buffer ABI remains unchanged.

The core builds an ordered access plan using address values as indices into
owned bytes. Overlapping source and destination ranges share that byte image.
Pixel, row and plane traversal preserve writes visible to subsequent reads;
planar alpha blits process alpha first, while solid fills process Y/U/V first.
Packed blits retain the bpp-dependent starting offset and DWORD output stride.
Inverse alpha, wrapping channel additions, clipping and signed pitches follow
the reference operations rather than a normalized pixel-format description.

Each copy snapshots one row. The archived x86/x64 CRT symbol maps resolve
memcpy and memmove to the same address; executable comparisons must establish
the admitted overlap cases. A later row still sees prior writes. Initial-read
planning excludes bytes initialized by earlier writes, including a copy whose
destination becomes the next row's source. Untouched bytes and padding are not
sampled or copied merely because they lie between touched ranges.

One private FFI export detaches a 48-byte scalar packet, imports necessary
initialized byte ranges, executes the safe plan, and exports only written runs.
Original pointer provenance is retained through address adjustments; no Rust
reference to host memory is constructed or retained. Three documented unsafe
copy sites implement this boundary; there is no new assembly or shared registry.
The native call wrapper preserves FP controls/status, errno and LastError.

Callers must lend readable initialized initial-read ranges and writable output
ranges within the original allocations. Pixel ranges may overlap. Metadata
aliases, invalid backing storage, concurrent access, allocation failure and
overflowing geometry remain unclosed. Negative memcpy counts and overflowing
copy sizes return an unresolved error rather than silently becoming empty work.
The separate FFI ownership test uses an initially uninitialized destination that
becomes readable only after a prior row writes it.

Direct Windows comparisons are pending at this implementation checkpoint. The
observer covers 11,916 cases per architecture, twice per role, with two operations
per case. Lossless deltas compare every byte in initialized 4,096-byte backing
allocations, including guard regions, plus native allocation and FP/error state.
Reference disassembly and provenance are archived under
`test/compat/surface-reference`; inspection captures alone establish no parity.
Experimental switches remain opt-in. Frame/creator state, color conversion,
caches, full hosts and vendors remain part of the whole-library migration.

The initial observer at `29c9e736` failed compilation on both architectures
because its std::array header was missing. No probe executable ran. The header
correction changes only the observer; production code is unchanged. Failed
reports remain under `artifacts/surface-owned-initial-{x86,x64}`.

The first executable observer at `474275d6` crashed in the reference on both
architectures. Its 3,072-byte fixture was too small for unclipped solid fills
at right/bottom positions combined with a cross-row alias (up to byte 3,212).
These are rejected comparisons, preserved under `surface-owned-final-*`.
The corrected observer uses 4,096 bytes and checks conservative source/output
footprints before invoking native code. This fixes the harness, not production;
no memory-corruption outcome is accepted as compatible.

Review after the first bounded comparisons found unnecessary descriptor reads
in the solid-fill adapter. The corrected packet never reads descriptor width
or bpp, and reads height only for planar fills. The observer now deliberately
leaves those unused fields uninitialized. Earlier focused/corpus jobs were
stopped before changing tracked sources and are not complete-suite evidence.
The final build must rerun all comparisons at this corrected checkpoint.
