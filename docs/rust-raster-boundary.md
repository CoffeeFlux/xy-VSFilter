# Rust raster construction boundary

The experimental `Rasterizer::Rasterize` path consumes checked Rust scan and
widened-region IDs directly. `rust/compat/src/raster.rs` owns dimension and offset
calculation, border rounding, fully initialized body/border storage and ordered
span accumulation. It does not export an owner pointer or mutable pixel loan.
The reference is CoffeeFlux cleanup commit
`67da07cda068088b93e6de300f56cbad15ab6c5d`, specifically `Rasterizer::Rasterize`.

The eight-field little-endian packet contains offset X/Y, width/height, overlay
width/height/pitch and flags, followed by full body and optional border planes.
Flag bits indicate empty widened outline, body presence and border presence.
Either zero scan dimension returns the 32-byte cleaned state, with all flags
false, before any span access. Otherwise subpixel offsets are masked to three
bits; an existing widened outline expands both axes by its border rounded to
eight. Native low-word arithmetic is explicit. Pitch/height products and all
linear span addresses are checked before access; byte coverage wraps.

Border spans accumulate before body spans. The source's packed coordinate bias
and translation order are preserved. Linear addressing can intentionally cross
a row boundary; it is not clipped to logical row width. Every allocation byte,
including padding, is initialized before accumulation and snapshot reads.
Invalid addresses, unsupported signed shifts and allocation/address overflow
return unresolved status 6 rather than corrupting memory or retrying natively.

The existing atomic owner admission now includes a raster packet registry. A
create operation borrows the scan and region while constructing and inserting
the result. IDs are monotonic and checked. Read checks the entire caller capacity
before copying under admission; invalid/short requests do not write. No native
callback occurs, no borrowed reference escapes and release rejects stale IDs.
The two new unsafe exports have three copies in two unsafe blocks, all limited to caller
output buffers; construction and packet ownership remain safe Rust.

The C++ bridge holds the existing native gate across create/read/release and uses
RAII for release even if snapshot allocation throws. The result is then copied
into transitional `PlaneAllocation` buffers for the existing overlay consumers.
Body allocation/attachment precedes border allocation/attachment. These copies
retain existing shared-pointer alias behavior and `g_xy_malloc_used_size`
increments/decrements, which affect cache eviction. The packet is temporary
scratch and is not counted as a cached overlay. C++ never receives a pointer into
the canonical raster packet.

This ports raster construction, **not the full overlay object graph**. Blur,
subpixel variance, clipping and drawing still use detached native-facing planes.
Those interfaces, their mutable loans and shared aliases remain migration work.
In particular Gaussian blur leaves some padding unwritten; the fully initialized
raster contract must not be generalized to Gaussian output without evidence.
Allocator failure, partial native failure state, invalid geometry, Debug builds,
other FP controls and arbitrary host concurrency remain unclosed. There is no
production-default switch.

The maintained `tools/compat/raster_native.py` observer invokes actual cleanup
and candidate methods twice each on Windows x86/x64. It covers 512 reused-overlay
calls, 16 paths, eight radius pairs and four signed/extreme subpixel offsets.
Traces contain metadata, every initialized plane byte including padding, old body
aliases retained across replacement, cache allocation-count deltas and FP/errno/
LastError state. The validator rejects incomplete/reordered calls, missing planes,
invalid geometry/accounting and unstable reference repeats. Numerical and pixel
parity claims require completed reports; safe-core tests alone do not prove them.
