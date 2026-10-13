# Compatibility-first Rust migration

## Contract

The behavioral reference is pinterf/xy-VSFilter commit
`4911cacb102bc0cbd41449793e54473265b04e62`. All functionality remains in scope.
The target is a drop-in replacement in players, Aegisub and other existing hosts,
with bit-identical output. The repository itself defines ASS behavior; another
renderer or format description must not override it. Memory safety is the reason
for migration. A performance regression is acceptable. No deliberate semantic
cleanup is part of a port.

Preserve deterministic malformed-input results, parser recovery, byte and code
unit handling, integer truncation, timing boundaries, rounding, font metrics,
rendering order, caches' visible effects, and state across seeks. Keep externally
observable settings, interfaces, exports, identity and calling conventions.
Stable behavior implemented with unsafe C++ may need safe emulation in Rust.
Do not reproduce memory corruption. Crashes, hangs and unstable output are
unresolved reference outcomes requiring investigation, never matching results.

## What has been implemented

* A dependency-free Cargo workspace. `rust/compat` forbids unsafe Rust.
* A safe port of `CTextFile::NextChar`, including its nonstandard UTF-8 behavior,
  raw UTF-16 code units, error statuses and cursor movement.
* A value-only internal C ABI: no foreign pointers, allocations or ownership
  enter Rust. A C++ adapter packs up to four bytes and updates the original cursor.
* An opt-in `RustTextDecoder=true` native build property. The original public DLL
  interfaces and the surrounding line reader are retained during this step.
  The default build still uses the original decoder, pending Windows validation.
* A differential test that extracts the exact original method and UTF-8 helper
  header from the pinned Git commit, compiles them, and calls the real Rust ABI.
  It checks **85,992,064** cases: return values AND consumed bytes. This includes
  all 256 possibilities for each UTF-8 tail byte, not merely valid Unicode.
* An isolated-process CSRI comparison harness, an exact-byte corpus generator,
  and a PE export/ordinal comparison tool.
* CI for macOS, Linux and Windows x86/x64 leaf/bridge tests. These jobs do not
  build or validate the complete legacy renderer.

The migration is an initial component and validation foundation, not a finished
safe renderer. The rest of the native parsing, font handling, rendering,
buffer management and host integration remains C++. There is no whole-renderer
compatibility or memory-safety claim yet.

## A concrete quirk already preserved

The UTF-8 branch of `NextChar` only validates the leading byte and available
length. It masks subsequent bytes with `0x3f` even when they are not continuation
bytes. For example, `c2 41` produces `0x81`, `ed a0 80` produces the raw surrogate
`0xd800`, and `f4 bf bf bf` produces `0x13ffff`. The latter is handled later by
`ReadLine`, not by this decoder. A rejected lead consumes one byte; an incomplete
sequence consumes none. Rust strings or `char` would lose this behavior.

## Local validation

Python 3.10+, a C++ compiler, Git history containing the pinned commit, and Rust
are sufficient. The CI toolchain is Rust 1.85.0; local reports record the actual
compiler. No Cargo registry downloads are needed by the workspace.

```sh
cargo test --workspace --locked --offline
cargo clippy --workspace --all-targets --locked --offline -- -D warnings
cargo fmt --all --check
python3 tools/compat/check_decoder.py --sanitize
python3 -m unittest discover -s tools/compat -p 'test_*.py' -v
python3 tools/compat/make_corpus.py --check
```

The decoder report goes to `artifacts/decoder/<target>/report.json` and records
the reference commit, source hashes, compiler and result. The native tests
extract the original algorithm without modifying it; the only test stand-in is
the class shell around that method. The ASan/UBSan run instruments the C++ side;
it does not instrument the Rust static library. Safe Rust is enforced in the
core by a compiler lint. The FFI export has no unsafe blocks.

Harness tests compile a deliberately fake native CSRI library, exercise the real
ctypes ABI and subprocess machinery, and inject byte changes, crashes, hangs,
rejections and buffer overwrites. Passing these tests validates the harness,
not VSFilter's output.

## Windows handoff

1. Clone `https://github.com/CoffeeFlux/xy-VSFilter.git`, fetch all history, and
   check out `rust/compatibility-foundation`.
2. Use separate baseline and candidate worktrees/build directories. The baseline
   must be the pinned commit above, without the Rust branch's code changes.
   Use the same Visual Studio toolset, SDK, Release configuration, architecture,
   Windows installation, fonts, locale and settings for each pair. Preserve the
   binaries and record their hashes. Do not mix x86 and x64 binaries.
3. Install Visual Studio C++ build tools including MFC/ATL, the Windows SDK,
   and the YASM integration needed by the original projects. The existing
   `build_vsfilter.sh` and `.github/workflows/build.yml` document the historical
   VS2019 build; `src/configuration.props` also selects v143 for VS2022. The
   legacy workflow uses old Actions/runner versions, so don't assume it still
   works unchanged. Restore a baseline build before interpreting port failures.
4. Install Rust's MSVC targets:

   ```powershell
   rustup target add x86_64-pc-windows-msvc i686-pc-windows-msvc
   ```

5. In the appropriate VS Native Tools prompt, run the leaf differential test:

   ```powershell
   python tools/compat/check_decoder.py --target x86_64-pc-windows-msvc
   # Repeat in an x86 compiler environment:
   python tools/compat/check_decoder.py --target i686-pc-windows-msvc
   ```

6. Build the candidate Rust library, then rebuild the native solution with the
   experimental property. Start with x64; repeat for Win32:

   ```powershell
   powershell -File tools/compat/build_rust_windows.ps1 -Platform x64
   msbuild VSFilter.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RustTextDecoder=true
   ```

   The Rust library uses a static CRT to match `/MT`. Debug integration is
   intentionally rejected until its runtime configuration is validated. The
   build script prints `native-static-libs`; check the list against the native
   link configuration when changing Rust versions. Rebuild all dependent native
   libraries after changing the property; existing objects are not trustworthy
   when switching implementations.

7. Compare exports for every shipped DLL, including the consumer when built:

   ```powershell
   python tools/compat/pe_exports.py C:\baseline\VSFilter.dll C:\candidate\VSFilter.dll
   python tools/compat/pe_exports.py C:\baseline\XySubFilter.dll C:\candidate\XySubFilter.dll
   ```

8. Run original-vs-original first, then original-vs-candidate. Python must have
   the same architecture as the DLL. Output directories must be new:

   ```powershell
   python tools/compat/csri_compare.py --reference C:\baseline\VSFilter.dll --candidate C:\baseline\VSFilter.dll --output artifacts\reference-x64 --environment-label "Win-build/toolset/fonts/settings"
   python tools/compat/csri_compare.py --reference C:\baseline\VSFilter.dll --candidate C:\candidate\VSFilter.dll --output artifacts\candidate-x64 --environment-label "Win-build/toolset/fonts/settings"
   ```

9. Investigate every mismatch or unstable reference before enabling the port by
   default. Smoke-test actual Aegisub loading and rendering, DirectShow playback,
   AviSynth and VirtualDub. Add automated host-specific coverage as those seams
   become accessible. CSRI alone cannot validate the complete product.

## CSRI corpus and evidence

`test/compat/cases.json` initially contains 146 cases: both file and memory
loading for 34 existing ASS samples and 38 generated samples, plus geometry and
stride cases. Generated cases include malformed tags, duplicate tags, malformed
UTF-8, unpaired UTF-16, embedded NUL, CR/LF differences, missing final newlines
and the original 4096-byte refill boundary. `.gitattributes` prevents checkout
from changing any test bytes. Run `make_corpus.py` to regenerate, or `--check`
to verify. Do not rewrite old fixtures to make a test pass.

Timestamps sample the first recognizable event's boundaries, midpoint and
backward seeks. This is deliberately a starter corpus: it does not cover all
events, animations, historical issue-specific timestamps, formats or host paths.
Sampling times are configuration, not a specification of how ASS is parsed.
Grow coverage using real scripts and minimized mismatch reproducers.

Each case gets fresh reference processes (at least two) and a fresh candidate
process, but retains one renderer instance for its entire timestamp sequence.
The complete destination allocation, including row padding and the fourth byte,
is initialized identically before each render and compared without tolerance.
Frame output is compared byte-for-byte; canaries detect nearby writes outside
the destination. Canaries are not a replacement for a sanitizer.

Reports include DLL and corpus hashes, case definitions, exact IEEE timestamp
bits, frame hashes, rejection statuses and first differing byte offsets. Failed
cases retain raw frames. Equal cases drop raw frames by default to save space;
`--keep-equal-frames` preserves all of them. Reports are checkpointed as cases
finish and only marked complete at the end. Crashes, timeouts, guard corruption,
harness errors and unstable reference output fail the run. A run with no rendered
frames cannot pass. Stable matching open rejections are recorded explicitly.

On Windows, reports also capture exported names/ordinals, OS, pointer width,
locale/code pages, hashes of files in system/per-user font directories, and a
read-only snapshot of `HKCU\Software\Gabest`. A change in that environment during
the run fails the comparison. Font registrations outside those directories,
system rasterizer versions, external files, host state and private fonts need
additional provenance as coverage grows. Keep baseline/candidate `.properties`
and localization sidecars identical and document them in the environment label.
The recorded reference commit identifies the intended source; the harness cannot
prove which source produced an arbitrary DLL. Archive build logs with binaries.

One source-level CSRI trap: `csri_request_fmt` accepts BGR32, BGR24, YUY2 and
YV12, but `csri_render` implements only BGR32 and otherwise displays a dialog and
enters invalid arithmetic. The harness records format-query results but only
renders BGR32. This mismatch is recorded, not silently "corrected" or mistaken
for implemented pixel-format coverage.

## Coverage map and remaining migration

| Surface/subsystem | Reference locations | Required evidence still missing |
| --- | --- | --- |
| DirectShow/COM, registration, property pages, autoload | `DirectVobSubFilterDll.cpp`, `DirectVobSub*.h`, `auto_load_helper.cpp` | CLSIDs/IIDs, vtables, HRESULTs, refcounts, graph negotiation, threading, registration/unload, player tests |
| XySubFilter and consumer | `xy_sub_filter*.cpp`, `include/SubRenderIntf.h`, `XySubRenderIntf.h` | Provider/consumer negotiation, frame ownership, options and lifecycle |
| Aegisub/CSRI | `csri.h`, `csriapi.cpp` | Real DLL corpus runs, discovery/metadata/flags, format queries, lifecycle and real Aegisub integration |
| AviSynth | `plugins.cpp`, `vfr.cpp` | Exact `VobSub`, `TextSub`, `TextSubSwapUV`, `MaskSub` signatures/defaults, frame properties, timing, pixel formats, MT behavior |
| VirtualDub | `plugins.cpp`, bundled SDK headers | Export calling conventions, filter negotiation, scripting, RGB orientation, lifecycle |
| Text formats | `STS.cpp`, `TextFile.cpp`, `RealTextParser.cpp`, `USFSubtitles.cpp`, `libssf` | SSA/ASS, SubRip variants, SubViewer, MicroDVD, SAMI, WebVTT, VPlayer, XombieSub, USF, MPL2, RealText and SSF parsing/recovery |
| Bitmap subtitles | `VobSub*`, `HdmvSub*`, `DVBSub*`, `CompositionObject*`, `CCDecoder*` | DVD/VobSub, PGS/SUP, DVB, closed captions, embedded stream and external file behavior |
| Layout/rendering | `RTS.cpp`, `Rasterizer.cpp`, caches, overlay/clip code | Every tag's recovery and interaction, drawings, clipping, transforms, karaoke, blur/border/shadow, GDI/font metrics and fallback |
| Compositing/color | `subpic`, SIMD/assembly, conversion tables | All RGB/YUV formats, high bit depths, interlace/subsampling, pitch/orientation, alpha and rounding |
| Supporting behavior | `dsutil`, `decss`, vendored libraries, settings | Encodings, code pages, font attachments, file/network access, archive inputs, persistence and remaining unsafe dependencies |

The next useful work is a Windows baseline + full DLL comparison for this decoder,
then extracting the text reader/parser in bounded steps. Preserve bytes and UTF-16
units until the original code itself converts them. Keep GDI font behavior while
porting surrounding algorithms. Third-party native code also belongs in the
memory-safety inventory; converting first-party code alone does not establish a
safe Aegisub dependency. Use fuzzing and minimized differential cases to discover
the long tail, with a pinned reference and isolated workers for every experiment.
