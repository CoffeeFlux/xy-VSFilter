# Owned bitmap initialization and drawing

Reference: CoffeeFlux cleanup `67da07cda068088b93e6de300f56cbad15ab6c5d`.
The experimental path moves XyBitmap initialization, Rasterizer Draw and
AdditionDraw traversal, final packed alpha inversion and the optional debug
outline into safe Rust. Canonical bitmap owners survive native output-view and
cache lifetimes. Drawing consumes canonical overlay and composite-mask handles;
no image pointer crosses into the safe core.

Packed initialization writes logical pixels only, leaving padding unknown.
Planar initialization covers all four planes including padding. Detached native
views retain the original pitch, alignment residue, plane pointers, zero-sized
allocation identity and xy allocation accounting. Only known bytes are copied
out; uninitialized bpp is not read or assigned. The existing shifted allocation
helper now serves as a transitional output view rather than authoritative pixels.

Packed multicolor drawing retains color across rows while resetting its switch
cursor. Planar drawing advances the switch cursor past a backward boundary
without moving its pixel cursor backward. Addition translates boundaries to the
clipped origin and then walks each row. These differ from CompositeAlphaMask,
which can write before dirty. Scalar Rust reuses the established exact pixel
blend arithmetic; complete drawing still needs linked comparison across widths.

Six private FFI exports manage bitmap creation, size/read, drawing, final actions
and destruction under the existing path-owner gate. Inputs are detached scalar
packets; production switches contain six initialized DWORDs. Switch storage is
read only for an active nonempty draw. Output capacity is checked before copying.
The existing shared native bitmap object retains its canonical owner until final
release; source overlays and composite masks need not outlive the result.

Unsupported bitmap layouts, invalid addresses, unknown pixels actually consumed,
exhausted switch arrays and overflowing allocations remain unresolved errors.
Production DrawItem routes planar addition to ordinary Draw; direct unsupported
planar AdditionDraw diagnostics remain unclosed. Allocation failures, arbitrary
aliasing/concurrency, debug-macro integration and complete host/vendor migration
remain unclosed. Experimental switches stay opt-in.

Both Windows DLL builds and Rust unit/Clippy checks pass at this implementation
checkpoint. A new linked observer covers 3,528 drawing cases and 18 zero-sized
bitmap cases, repeated twice per role, with initialization, repeated draws,
final alpha, shared survival and release. Direct and whole-renderer comparison
results are pending. Reference functions are archived separately from runtime
evidence under `test/compat/bitmap-draw-reference`.
