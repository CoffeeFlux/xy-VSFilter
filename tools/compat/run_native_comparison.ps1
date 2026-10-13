param(
    [ValidateSet("Win32", "x64")][string]$Platform = "x64",
    [Parameter(Mandatory=$true)][string]$Baseline
)
$ErrorActionPreference = "Stop"
$candidateRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
$baselineRoot = (Resolve-Path $Baseline).Path
$evidence = Join-Path $candidateRoot "artifacts\native"
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$reference = Get-Content "$PSScriptRoot\reference.json" | ConvertFrom-Json
$baselineCommit = (& git -C $baselineRoot rev-parse HEAD).Trim()
if ($baselineCommit -ne $reference.commit) { throw "Baseline is not the pinned reference commit" }
if (& git -C $baselineRoot status --porcelain) { throw "Baseline has local changes" }

function Build-Native([string]$Root, [string]$Label, [bool]$Rust) {
    $arguments = @("$Root\VSFilter.sln", "/m", "/t:vsfilter;xy_sub_filter;xy_sub_filter_consumer",
        "/p:Configuration=Release", "/p:Platform=$Platform", "/p:BuildProjectReferences=false",
        "/bl:$evidence\$Label.binlog", "/fl", "/flp:logfile=$evidence\$Label.log;verbosity=normal")
    if ($Rust) { $arguments += "/p:RustTextDecoder=true" }
    & msbuild @arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label native build failed ($LASTEXITCODE)" }
}

function Find-Dll([string]$Root, [string]$Name) {
    $files = @(Get-ChildItem "$Root\bin" -Recurse -Filter $Name | Where-Object {
        $_.FullName -match "[\\/]$Platform[\\/]Release[\\/]"
    })
    if ($files.Count -ne 1) { throw "Expected one $Name for $Platform in $Root, found $($files.Count)" }
    return $files[0].FullName
}

Push-Location $candidateRoot
try {
    & "$PSScriptRoot\build_rust_windows.ps1" -Platform $Platform
    Build-Native $baselineRoot "baseline" $false
    Build-Native $candidateRoot "candidate" $true
    foreach ($name in @("VSFilter.dll", "XySubFilter.dll", "xy_sub_filter_consumer.dll")) {
        $original = Find-Dll $baselineRoot $name
        $candidate = Find-Dll $candidateRoot $name
        & python "$PSScriptRoot\pe_exports.py" $original $candidate > "$evidence\$name.exports.json"
        if ($LASTEXITCODE -ne 0) { throw "Export mismatch: $name" }
    }
    $original = Find-Dll $baselineRoot "VSFilter.dll"
    $candidate = Find-Dll $candidateRoot "VSFilter.dll"
    & python "$PSScriptRoot\csri_compare.py" --reference $original --candidate $candidate `
        --output "$evidence\csri" --timeout 15 `
        --environment-label "GitHub windows-2022; VS2022 Release $Platform; stock runner fonts; pinned baseline"
    if ($LASTEXITCODE -ne 0) { throw "CSRI comparison failed; inspect artifacts/native/csri/report.json" }
} finally {
    Pop-Location
}
