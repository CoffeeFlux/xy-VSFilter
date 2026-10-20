# Owned clip-mask construction

Reference: CoffeeFlux cleanup `67da07cda068088b93e6de300f56cbad15ab6c5d`.
The experimental `CClipper::Paint` path now creates a Rust request before native
polygon painting and finishes against the canonical Rust overlay. Rust owns
geometry, inversion, initialized pixels, validity masks, banner/scroll fades
and mask lifetimes. Request preparation preserves scale conversion before base
early returns; scroll endpoint conversion follows successful base construction.

Two fixed-local assembly sites reproduce x86 integer/double spill/multiply and
CRT `_ftol2_sse`, or x64 `cvtdq2pd`/`mulsd`/`cvttsd2si`. Raw scale bits cross the
boundary without native floating copies. No image pointer or traversal enters
the numeric backend. Linked functions and conversion helpers are archived in
`test/compat/clipper-numeric` with pinned provenance.

Simple clips retain an independent canonical Rust snapshot, including unknown
padding. Their detached native view continues to share the source overlay's
body, preserving public alias lifetimes and native allocation accounting.
Base masks own fully initialized target-sized pixels. Native `GrayImage2`
copies clone the Rust owner and retain their existing shared native view.
These views remain transitional until downstream compositing is ported.

Arithmetic preserves byte wrap after inversion and signed multiplication/shift
in fades, including repeated multiplication where banner margins overlap.
Negative fades are not silently clamped. Zero fade on a nonempty base faults
in the reference and is returned as unresolved error 6 by the checked boundary;
invalid native addresses, unknown logical input and impossible allocations are
also unresolved rather than sanitized pixels or matching corruption.

Rust ownership tests and x86/x64 builds are implemented. Direct native clipper,
full renderer and host regression validation is pending for this checkpoint.
The experimental switches remain opt-in; full renderer/host/vendor migration
remains active.
