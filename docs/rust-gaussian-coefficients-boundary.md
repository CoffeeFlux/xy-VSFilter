# Gaussian coefficient construction

The experimental Gaussian coefficient constructor now calls a checked Rust
operation with raw sigma bits. Safe Rust owns the coefficient vector, symmetric
recurrence indexing, normalization traversal, zero tail and detached packet.
Packets contain radius, logical/rounded widths, allocation presence, stored sigma
bits and every initialized coefficient bit. IDs are monotonic, reads check capacity
before writing, and stale reads/releases fail. No coefficient address is exported.

The target numeric backend has only fixed offsets in an initialized aligned
256-byte local. x86 reproduces the linked x87 exponential sequence and CRT ceil/
integer conversion. Recurrence and volume state use 80-bit slots between calls;
narrowing them to double would change the reference. x64 uses the captured SSE
order, the same CRT exp routine and the captured inline ceil sequence. Both
evaluate the exponential before the constructor's zero-sigma early return.
The core preserves signed width metadata and reports overflowing positive
allocation geometry as unresolved; it does not clip coefficients or sanitize NaNs.

The native facade reads a completed packet into an aligned detached snapshot,
then releases the construction owner with RAII. Existing ShiftedPlaneStorage owns
that snapshot and preserves logical allocation accounting and physical prefix.
Copy construction remains independent. The native weak coefficient cache remains
transitional: it still controls sharing and reconstruction frequency, including
its floating equality/hash behavior. Gaussian image orchestration, floating SSE
filter dispatch, transposes and the remaining host/native surfaces stay in scope.

`test/compat/gaussian-coefficients-numeric` preserves the original linked cleanup
arithmetic and constants. The dedicated observer declares the cleanup object
layout and calls its linked private init method from pinned subtitlesR.lib. Only
the constructor's initial field assignments, ordinary byte copy and destructor
are reproduced by the observer. The candidate uses the same facade header as
production. It compares raw coefficient/sigma bits, shape, sharing, cache counts,
allocation deltas and FP/errno/LastError through construction, copy and release.
Default FP controls, sticky division-by-zero, repeated lifetimes, signed zero,
negative/subnormal/nonfinite inputs and rounding boundaries are included.

The actual Gaussian/combined rendering observer and full Windows corpus are also
required before claiming rendering parity. Coefficient tests alone do not close
the renderer. Allocator failure, other FP controls, Debug, native invalid image
geometry, partial failure state and arbitrary concurrent host calls remain
unclosed. The experimental switch remains opt-in.
