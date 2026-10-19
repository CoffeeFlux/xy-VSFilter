# CWord transform boundary

The experimental renderer transforms the existing Rust-owned path directly.
`CWord::Transform` copies nine native double bit patterns and two signed origin
coordinates into an 81-byte private packet. It does not copy floating values by
assignment, export a Rust address, or edit a detached native path. The path
registry validates the ID under its existing exclusive admission. Safe core
iteration replaces each point and performs wrapping `(coordinate + 32) & ~63`
when the native short-circuit scale comparisons request it. Types stay intact.

`windows_word_transform.rs` supplies arithmetic for the cleanup reference
`67da07cda068088b93e6de300f56cbad15ab6c5d`. The reference was compiled with MSVC
14.44.35207, Win32 `/arch:IA32` and x64 SSE2. The retained disassemblies and binary,
library and trace hashes are in
[`test/compat/word-transform`](../test/compat/word-transform/provenance.json).
These are observations of this compiler configuration, not an ASS specification
or a guarantee for arbitrary compiler and floating-control configurations.

An ordinary sequence of Rust `f64` expressions is insufficient here. The x86
reference uses FCOS/FSIN, extended x87 intermediates, deliberate f64 spills and
the CRT `_ftol2_sse` conversion. In particular, the first point consumes retained
extended `z22`; later points reload its f64 spill. The x64 reference uses CRT
cos/sin, SSE2 arithmetic, MAXSD and CVTTSD2SI. Both kernels preserve instruction
and NaN operand order, signed-origin negation, conversion overflow results and
floating status. Preparation runs even when the path is empty. Scale comparisons
occur after each point's conversion, only when the round flag is enabled.

## Memory and calling convention audit

Each `Numeric` owns a zero-initialized `[u64; 96]` (768 bytes). Assembly receives
only the address of that local array, never a path, registry, native object or
caller-provided buffer. Every memory operand is a constant offset; no count,
point coordinate, floating value or foreign address can influence an address.

| Byte offsets | Contents |
| --- | --- |
| 0–303 | Native scalar spill slots; offsets match the reference stack slots |
| 320–359 | x64 retained matrix scalars |
| 400–409 | x86 raw 80-bit first-point `z22` |
| 416–423 | One initialized point, copied by safe Rust |
| 424–431 | Two converted integer results |
| 512–583 | Nine input double bit patterns |
| 584–591 | Signed origin coordinates |
| 640–687 | 100, radians-per-degree bits, 20000, 0.5, 1000 and 1 |

The four architecture-specific preparation/conversion blocks use only these
regions. x86 preparation has no calls and empties its x87 stack by storing the
retained 80-bit value. Per-point conversion recreates the native stack, calls
`_ftol2_sse` twice and removes its remaining constants before returning. EDI is
callee-preserved and all C volatile clobbers are declared. There is no pointer
arithmetic over a path in assembly.

x64 preparation supplies 32 bytes of shadow space for each CRT call, preserves
the assembly entry alignment and restores RSP. RDI is callee-preserved. Explicit
XMM outputs cover the nonvolatile registers used by the translated instruction
sequence, in addition to declared C ABI clobbers. Point conversion has no calls
or stack effects. The separate scale comparison declares its scalar loads,
condition flags and register outputs; an ordinary Rust comparison could remove
observable denormal/signaling-NaN effects.

The CRT arithmetic calls are native numeric dependencies. They receive scalars
only and cannot access the path. This boundary does not claim compatibility for
unmasked floating exceptions, arbitrary occupied x87 stacks, allocation failure,
Debug builds or every host concurrency history. It does not complete the library
migration. Scan conversion, polygon preparation, native object graphs, host
adapters and vendored code remain in scope.

## Reproduction

Build the cleanup checkout and experimental candidate libraries for each
architecture with the repository's documented Windows configuration. From the
matching Visual Studio developer shell, run:

```text
python -X utf8 tools/compat/word_transform_native.py --reference-root artifacts/cleanup-reference --platform Win32 --output artifacts/transform-x86
python -X utf8 tools/compat/word_transform_native.py --reference-root artifacts/cleanup-reference --platform x64 --output artifacts/transform-x64
```

The runner requires clean committed sources, hashes the inputs, and checks two
complete traces per role. It calls the actual private CWord method through a
volatile member-function pointer to prevent replacement by an inlined probe
implementation. The 4,560 cases cover 126 single-field raw inputs, 32 mixed finite
and 32 mixed raw inputs, sticky divide-by-zero state, three origins including
integer limits, both rounding flags, and empty/nonempty paths. It compares every
point/type/hash, independent x87/SSE status, errno and LastError. Negative controls
reject missing/reordered cases, altered inputs, points, hashes, status and repeats.
