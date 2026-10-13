param(
    [ValidateSet("Win32", "x64")][string]$Platform = "x64"
)
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
$rustTarget = if ($Platform -eq "Win32") { "i686-pc-windows-msvc" } else { "x86_64-pc-windows-msvc" }
$previousFlags = $env:RUSTFLAGS
Push-Location $repoRoot
try {
    # The original Release projects use /MT. Match it; never share allocations.
    $env:RUSTFLAGS = "-C target-feature=+crt-static"
    & cargo rustc --locked --offline --release -p xy-vsfilter-ffi --features native-runtime --target $rustTarget -- --print native-static-libs
    if ($LASTEXITCODE -ne 0) { throw "Rust build failed ($LASTEXITCODE)" }
    Write-Host "Built target\$rustTarget\release\xy_vsfilter_ffi.lib"
    Write-Host "Rebuild ALL native projects with /p:RustTextDecoder=true; do not reuse baseline objects."
} finally {
    $env:RUSTFLAGS = $previousFlags
    Pop-Location
}
