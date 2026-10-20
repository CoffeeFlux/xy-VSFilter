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

Validated at `9b0f04e1` on x86 and x64: 5,152 direct clipper cases, fifteen
focused probes including 17,924 overlay cases, the full 1,222-case / 13,186-frame
CSRI corpus and 192 distinct controls, all three DLL builds/exports/dependencies,
and ten scoped COM cases plus two pin-ID controls. Rust tests passed 385 core
and 142 FFI cases per architecture, formatting and Clippy; Python passed 218
tests with one existing skip in the configured shell. See
[the evidence](rust-clip-owned-local-evidence.json) for exact hashes and scope.
The experimental switches remain opt-in; full renderer/host/vendor migration
remains active.
