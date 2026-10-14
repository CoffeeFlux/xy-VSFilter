param(
    [ValidateSet("Win32", "x64")][string]$Platform = "x64",
    [Parameter(Mandatory=$true)][string]$Baseline
)
$ErrorActionPreference = "Stop"
$candidate = (Resolve-Path "$PSScriptRoot\..\..").Path
$baselineRoot = (Resolve-Path $Baseline).Path
$evidence = Join-Path $candidate "artifacts\ssf-native\$Platform"
if (Test-Path $evidence) { throw "Preserve existing SSF evidence: $evidence" }
New-Item -ItemType Directory -Path $evidence | Out-Null
$reference = Get-Content "$PSScriptRoot\reference.json" | ConvertFrom-Json
if ((& git -C $baselineRoot rev-parse HEAD).Trim() -ne $reference.commit) { throw "Wrong reference commit" }
if ((& git -C $baselineRoot status --porcelain) -or (& git -C $candidate status --porcelain)) { throw "Use clean committed sources" }
$target = if ($Platform -eq "Win32") { "i686-pc-windows-msvc" } else { "x86_64-pc-windows-msvc" }
& "$PSScriptRoot\build_rust_windows.ps1" -Platform $Platform
$build = @{
    reference_commit = $reference.commit
    candidate_commit = (& git -C $candidate rev-parse HEAD).Trim()
    platform = $Platform
    msvc_tools_version = $env:VCToolsVersion
    windows_sdk_version = $env:WindowsSDKVersion
    libraries = @{}
    commands = @{}
}
foreach ($side in @("reference", "candidate")) {
    $root = if ($side -eq "reference") { $baselineRoot } else { $candidate }
    $arguments = @("$root\src\subtitles\libssf\libssf.vcxproj", "/m", "/p:Configuration=Release", "/p:Platform=$Platform",
        "/p:SolutionDir=$root\", "/p:BuildProjectReferences=false", "/bl:$evidence\$side.binlog",
        "/fl", "/flp:logfile=$evidence\$side.log;verbosity=normal")
    if ($side -eq "candidate") { $arguments += "/p:RustTextDecoder=true" }
    & msbuild @arguments
    if ($LASTEXITCODE -ne 0) { throw "$side SSF library build failed" }
    $libraries = @(Get-ChildItem "$root\bin" -Recurse -Filter "libssfR.lib" | Where-Object { $_.FullName -match "[\\/]$Platform[\\/]" })
    if ($libraries.Count -ne 1) { throw "Expected one $side SSF library" }
    $library = $libraries[0].FullName
    Copy-Item $library "$evidence\$side.lib"
    $build.libraries[$side] = @{sha256 = (Get-FileHash $library -Algorithm SHA256).Hash.ToLower(); path = $library}
    $compile = @("/nologo", "/O2", "/EHsc", "/std:c++14", "/MT", "/DNDEBUG", "/DUNICODE", "/D_UNICODE",
        "/DWINVER=0x0601", "/D_WIN32_WINNT=0x0601", "/DNTDDI_VERSION=NTDDI_WIN7", "/D_CONSOLE",
        "/I$root\src", "/I$root\src\filters\BaseClasses", "/I$root\include",
        "$PSScriptRoot\ssf_native_probe.cpp", "/Fe:$evidence\$side.exe", "/Fo:$evidence\$side.obj")
    if ($side -eq "candidate") { $compile += "/DXY_VSFILTER_RUST_TEXT" }
    $compile += @("/link", $library, "gdi32.lib", "user32.lib", "ole32.lib", "oleaut32.lib", "uuid.lib", "advapi32.lib", "shell32.lib", "comctl32.lib")
    if ($side -eq "candidate") { $compile += "$candidate\target\$target\release\xy_vsfilter_ffi.lib" }
    $build.commands[$side] = @{library = $arguments; probe = $compile}
    & cl @compile 2>&1 | Tee-Object -FilePath "$evidence\$side-probe.log"
    if ($LASTEXITCODE -ne 0) { throw "$side SSF probe link failed" }
}
$build.rust_library_sha256 = (Get-FileHash "$candidate\target\$target\release\xy_vsfilter_ffi.lib" -Algorithm SHA256).Hash.ToLower()
$build | ConvertTo-Json -Depth 12 | Set-Content "$evidence\build-info.json" -Encoding utf8
& python "$PSScriptRoot\ssf_native_compare.py" --reference "$evidence\reference.exe" --candidate "$evidence\candidate.exe" `
    --baseline-root $baselineRoot --platform $Platform --output "$evidence\comparison"
if ($LASTEXITCODE -ne 0) { throw "SSF native comparison failed; preserve its report" }
