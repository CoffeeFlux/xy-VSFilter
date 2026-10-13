# Rust migration constraints

The user requires a complete drop-in replacement for this repository: players,
Aegisub/CSRI, AviSynth, VirtualDub, DirectShow, XySubFilter and its consumer, all
formats, options and interfaces remain in scope. Performance may regress;
compatibility and memory safety take priority.

* The reference is commit `4911cacb102bc0cbd41449793e54473265b04e62`, not an ASS
  specification or another renderer. Preserve reproducible weird behavior,
  including malformed input, exact pixels, byte consumption, error handling,
  timing, rounding, encoding, and state across seeks. Do not silently sanitize.
* Compare candidate implementations directly with reference code or pinned
  reference binaries. Keep reference provenance and regression fixtures.
* A crash, hang, or nondeterministic result is an unresolved reference outcome,
  not a passing comparison. Record it; do not reproduce memory corruption to
  obtain compatibility. Stable visible quirks caused by unsafe code may still
  need safe emulation.
* Keep the portable core in safe Rust (`forbid(unsafe_code)`). Minimize and
  document native boundaries. Do not replace permissive byte/code-unit handling
  with Rust Unicode strings without differential evidence.
* Production switches require Windows x86/x64 integration and exact rendering
  comparisons. macOS leaf tests do not establish whole-renderer compatibility.
* Publish migration work to the CoffeeFlux fork, not the upstream repository.
* Keep `docs/rust-migration.md` current with validated work and Windows handoff.
