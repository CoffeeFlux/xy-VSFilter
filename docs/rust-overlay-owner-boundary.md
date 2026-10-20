# Persistent overlay owner and subpixel shifting

The experimental renderer now retains each completed overlay in the checked Rust
image registry. Raster construction transfers its result ID directly to the
overlay facade. Cleanup releases the ID. Copy/assignment creates an independent
Rust image while the transitional native pixel snapshots retain their original
shared-pointer aliases. `CWord::PaintFromPathData` updates a cache copy's offsets
through the owner; the cached source remains unchanged. Self-assignment is a no-op.

`Image` owns the metadata, byte values and optional per-byte initializedness mask.
No mask means every byte is initialized. Unknown bytes have private placeholder
values that are not materialized in native snapshots. The wire format has the
existing 32-byte metadata header, then all present planes' values, then one binary
validity byte per value. Import rejects nonbinary masks and malformed lengths.
Canonical storage addresses never cross the ABI. The older raster-only byte-read
API rejects an image with unknown bytes rather than exposing its placeholders.

Gaussian blur still runs native orchestration. Its completed output is imported
using only proven initialized ranges: logical width on every filtered-plane row;
for the unfiltered body, complete top/bottom border rows and logical width on the
middle rows. The vertical border comes from the existing integer filter radius,
with no additional floating-point calculation. BeBlur output imports the entire
plane because it starts with a complete zero fill. The bridge constructs an
initialized packet and never reads unknown native padding. Native blur failures,
allocator failures and partial construction state remain unclosed.

Subpixel variance runs entirely in safe Rust on these owners. It preserves
three-bit shift masking, wrapping offsets/dimensions, native null-result checks,
full-pitch versus row-width copy selection, body-before-border allocation/copy and
bilinear per-column history. Full-pitch copies carry unknown padding validity;
row copies leave the new padding initialized to zero. A bilinear read of unknown
logical pixels reports unresolved status 6. Unknown values are not clipped,
zeroed into an observable native image, or retried through a native pointer path.
Default/empty overlays retain the cleanup behavior of producing zero body and
border planes for admitted shifts. Native null-return leaks are not reproduced.

All image operations use the existing atomic registry admission. Borrowed owner
references stay inside the closure; output writes use detached scalars/packets.
Read checks capacity before writing. Clone, offset and variance reject stale IDs.
Zero is an explicit empty facade in clone/offset/variance, not an owner address.
The C++ facade uses RAII while transferring newly created variance IDs and
materializes only valid byte runs into detached aligned allocations. Existing
`g_xy_malloc_used_size` accounting continues to count those cached snapshots,
including native shared alias lifetimes; canonical Rust storage is additional
unaccounted implementation storage. Performance/memory usage may regress.

The native consumer audit found one overlay assignment and subsequent offset
update in `RTS.cpp`, now routed through the owner. Raster/BeBlur/Gaussian/variance
are the plane producers. `PaintSimpleClipper` shares a body snapshot and only
returns it for the noninverse/no-effect path; banner/scroll mutate separate
`PaintBaseClipper` storage. Draw/mask consumers read these snapshots. They remain
transitional interfaces, not completion of the GrayImage, clipping, blur or draw
ports. Arbitrary external mutation of this internal C++ facade is not a supported
owner operation. Host adapters, vendors and all remaining formats stay in scope.

The maintained actual-method observer is `tools/compat/overlay_native.py`: 5,124
calls from eight paths, four radii/settings, raster/BeBlur/Gaussian/combined blur,
cache-style assignment/offset changes, four subpixel shifts, chained shifts and
empty overlays. It compares metadata, all known bytes, FP/errno/LastError and
allocation-count deltas twice per role. Gaussian unknown padding is explicitly
outside byte parity. It also asserts copy aliases and unchanged source offsets.
The validator rejects missing/reordered calls, changed byte scope, incomplete
planes, invalid allocation deltas and unstable reference repeats. Completed
Windows x86/x64 reports are required before claiming parity for this checkpoint.
Unit tests alone are not that evidence. There is no production-default switch.
