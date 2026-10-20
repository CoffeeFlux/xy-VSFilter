# Owned Gaussian coefficient cache

Reference: CoffeeFlux cleanup `67da07cda068088b93e6de300f56cbad15ab6c5d`.
The experimental Gaussian operation now consumes raw strength/scale bits and
owns sigma products, weak coefficient sharing and coefficient lifetimes in Rust.
The C++ coefficient table and aligned coefficient snapshots are bypassed in the
production experimental Gaussian path. Both image passes were ported previously.
Combined Blur now runs in the safe core, including no-effect/empty-source returns, Gaussian-before-BeBlur sequencing and an owned intermediate. Downstream native consumers remain to be ported.

The safe cache retains initialized keys after nonreflexive NaN values expire.
Checked lease IDs retain one canonical value; its final release performs the
native key lookup and identity check before dropping coefficient storage.
Hashing, bucket traversal, insertion, rehash order and separate constructor,
deleter and stored-key copies follow the pinned table. Fixed-local Windows
arithmetic preserves x86 double-copy quieting, equality and growth FP effects.
No coefficient, image or cache pointer crosses the numeric boundary.

The direct observer compares native live coefficient allocation bytes with the
Rust cache's live initialized coefficient bytes. Detached observer packets are
excluded on both sides. Native cache eviction reads its allocation counter after
Gaussian scope ends, when both coefficient leases have been released. Whole
operation comparisons cover final overlay allocation accounting; arbitrary host
concurrency and partial allocation failure remain unclosed.

The complete checkpoint was validated at `87f77e41` on both Windows
architectures: 404 cache cases plus 192 retained-value drops; 400 actual
dispatch cases; 17,924 overlay cases; and 1,222 corpus cases / 13,186 frames
with 192 distinct controls. Scoped host contracts and the focused renderer
regressions passed. The direct cache first passed at `f133671e`; the later
checkpoint includes the complete Blur path. See
[the evidence](rust-blur-owned-local-evidence.json) for source/binary hashes,
initial observer failures and the unchanged historical unresolved outcomes.
The complete renderer, host surfaces and vendored native code remain in scope;
this experimental path is not a production-default switch or migration closure.

Standalone IsItReallyBlur and inlined Blur use separate captured numeric paths
on x86. The latter retains extended scalar values across its source-size check.
The backend keeps those values in initialized 80-bit local slots, with a balanced
hardware stack between calls. No-effect and empty-source returns publish no new
image ID and leave the native output untouched. Double-to-float argument copies
retain native order and rounding before the owned BeBlur operation.
