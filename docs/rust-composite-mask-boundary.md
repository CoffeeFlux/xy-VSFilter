# Owned composite alpha masks

Reference: CoffeeFlux cleanup `67da07cda068088b93e6de300f56cbad15ab6c5d`.
The experimental `CompositeAlphaMask` path moves clipping, color-segment
traversal, alpha arithmetic and initialized-byte ownership into safe Rust.
Canonical results survive native view copies and both Draw/AdditionDraw calls.
Native aligned views retain legacy allocation accounting and copy only bytes
the reference initializes. Downstream pixel drawing is still native.

Two reference quirks are deliberate. Every color segment restarts at the
initial clip-mask pointer. A boundary before the clipped start can cause a
later segment to write pixels before the dirty rectangle. Equal-alpha segments
coalesce before either behavior. The validity mask records the actual written
footprint, including this prefix; unknown allocation padding stays unknown.
Earlier preparation notes describing writes only inside dirty were incomplete.

Four FFI functions create, inspect, copy and release integer-keyed owners under
the existing path-owner gate. Parameters and results use detached byte packets;
no image pointer enters the safe core. Production DrawItem supplies six DWORD
switch arrays. The boundary reads two words for a single color and six for
multiple colors, only after geometry establishes nonempty output. Null switch
and other early returns do not read switch storage. Reads validate capacity
before writing. Stale owners, uninitialized inputs actually consumed, exhausted
switch histories and impossible addresses remain errors, never sanitized parity.

The portable implementation introduces no unsafe code or numeric assembly.
Source overlays and masks can be released independently after result creation.
The native adapter remains transitional. Allocation failure, arbitrary caller
addresses, malformed unbounded switch arrays, concurrency and full host/vendor
migration remain unclosed; experimental switches stay opt-in.

Linked cleanup functions are archived under `test/compat/composite-mask-reference`.
These captures establish provenance, not execution parity. A new actual-linked
observer covers 13,824 cases per architecture, repeated twice per role, with
initialized footprint bytes, geometry, floating-point/errno/LastError state,
native allocation deltas, copied-result survival and final release.

Validated at `69d9a694`: all 13,824 direct cases passed on x86 and x64, as did
sixteen focused probes, the full 1,222-case / 13,186-frame CSRI corpus with 192
distinct controls per architecture, all three DLL contracts and scoped COM
checks. Rust passed 389 core and 142 FFI tests per architecture plus formatting
and Clippy; Python passed 221 tests with one existing skip. See
[the evidence](rust-composite-owned-local-evidence.json) for exact hashes and scope.
The older extracted storage harness explicitly selects its original native
branch; its narrower historical evidence is not relabeled as owned-path parity.
