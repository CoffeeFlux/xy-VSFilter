param(
    [ValidateSet("Win32", "x64")][string]$Platform = "x64",
    [Parameter(Mandatory=$true)][string]$Baseline,
    [ValidateRange(0,256)][int]$MutationCount = 0,
    [uint32]$MutationSeed = 12345,
    [switch]$RustDocumentOwner,
    [string]$Output = ""
)
$ErrorActionPreference = "Stop"
$candidateRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
$baselineRoot = (Resolve-Path $Baseline).Path
$evidence = if ($Output) { [System.IO.Path]::GetFullPath($Output) } else { Join-Path $candidateRoot "artifacts\native\$Platform" }
if (Test-Path $evidence) { throw "Evidence directory already exists: $evidence. Choose a new -Output; preserve previous evidence." }
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$reference = Get-Content "$PSScriptRoot\reference.json" | ConvertFrom-Json
$baselineCommit = (& git -C $baselineRoot rev-parse HEAD).Trim()
if ($baselineCommit -ne $reference.commit) { throw "Baseline is not the pinned reference commit" }
if (& git -C $baselineRoot status --porcelain) { throw "Baseline has local changes" }
$candidateCommit = (& git -C $candidateRoot rev-parse HEAD).Trim()
$candidateStatus = @(& git -C $candidateRoot status --porcelain)
@{
    reference_commit = $baselineCommit
    candidate_commit = $candidateCommit
    candidate_status = $candidateStatus
    platform = $Platform
    configuration = "Release"
    rustc = @(& rustc -Vv)
    cargo = @(& cargo -V)
    msvc_tools_version = $env:VCToolsVersion
    windows_sdk_version = $env:WindowsSDKVersion
    mutation_count = $MutationCount
    mutation_seed = $MutationSeed
    document_storage = if ($RustDocumentOwner) { "rust-experimental" } else { "native" }
} | ConvertTo-Json -Depth 10 | Set-Content "$evidence\build-info.json" -Encoding utf8
& git -C $candidateRoot diff --binary "--output=$evidence\candidate-working-tree.patch" HEAD

function Build-Native([string]$Root, [string]$Label, [bool]$Rust) {
    $arguments = @("$Root\VSFilter.sln", "/m", "/t:vsfilter;xy_sub_filter;xy_sub_filter_consumer",
        "/p:Configuration=Release", "/p:Platform=$Platform", "/p:BuildProjectReferences=false",
        "/bl:$evidence\$Label.binlog", "/fl", "/flp:logfile=$evidence\$Label.log;verbosity=normal")
    if ($Rust) { $arguments += "/p:RustTextDecoder=true" }
    if ($Rust -and $RustDocumentOwner) { $arguments += "/p:RustDocumentOwnerExperimental=true" }
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
    if ($RustDocumentOwner -and !(Select-String -Path "$evidence\candidate.log" -SimpleMatch "XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL" -Quiet)) {
        throw "Experimental owner macro absent from candidate build log"
    }
    foreach ($name in @("VSFilter.dll", "XySubFilter.dll", "xy_sub_filter_consumer.dll")) {
        $original = Find-Dll $baselineRoot $name
        $candidate = Find-Dll $candidateRoot $name
        & python "$PSScriptRoot\pe_exports.py" $original $candidate > "$evidence\$name.exports.json"
        if ($LASTEXITCODE -ne 0) { throw "Export mismatch: $name" }
        & python "$PSScriptRoot\pe_dependencies.py" $original $candidate > "$evidence\$name.dependencies.json"
        if ($LASTEXITCODE -ne 0) { throw "Added imports or changed platform requirements: $name" }
    }
    & cl /nologo /O2 /EHsc /std:c++14 /MD "$PSScriptRoot\com_contract_probe.cpp" `
        "/Fe:$evidence\com-contract-probe.exe" "/Fo:$evidence\com-contract-probe.obj" `
        /link ole32.lib strmiids.lib uuid.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "COM contract probe build failed" }
    & python "$PSScriptRoot\com_contract_compare.py" --reference-root $baselineRoot `
        --candidate-root $candidateRoot --probe "$evidence\com-contract-probe.exe" `
        --platform $Platform --output "$evidence\com-contracts"
    if ($LASTEXITCODE -ne 0) { throw "COM contract comparison failed; inspect $evidence\com-contracts\report.json" }
    $original = Find-Dll $baselineRoot "VSFilter.dll"
    $candidate = Find-Dll $candidateRoot "VSFilter.dll"
    $manifest = Get-Content "$candidateRoot\test\compat\cases.json" -Raw | ConvertFrom-Json
    $smoke = @{schema=1; cases=@($manifest.cases | Where-Object { $_.id -eq "plain-memory" })}
    $smoke | ConvertTo-Json -Depth 10 | Set-Content "$evidence\smoke-cases.json" -Encoding utf8
    & python "$PSScriptRoot\csri_compare.py" --reference $original --candidate $candidate `
        --cases "$evidence\smoke-cases.json" --output "$evidence\smoke" --timeout 30 `
        --environment-label "GitHub windows-2022; VS2022 Release $Platform; initial DLL smoke check"
    if ($LASTEXITCODE -ne 0) { throw "DLL smoke comparison failed; inspect $evidence\smoke\report.json" }
    $documentStorage = if ($RustDocumentOwner) { "rust-experimental" } else { "native" }
    & python "$PSScriptRoot\document_native_lifecycle.py" --reference-root $baselineRoot `
        --candidate-root $candidateRoot --platform $Platform --document-storage $documentStorage `
        --output "$evidence\document-lifecycle"
    if ($LASTEXITCODE -ne 0) { throw "Native document lifecycle comparison failed; inspect $evidence\document-lifecycle\report.json" }
    & python "$PSScriptRoot\csri_compare.py" --reference $original --candidate $candidate `
        --output "$evidence\csri" --timeout 15 `
        --environment-label "GitHub windows-2022; VS2022 Release $Platform; stock runner fonts; pinned baseline"
    if ($LASTEXITCODE -ne 0) { throw "CSRI comparison failed; inspect $evidence\csri\report.json" }
    if ($MutationCount -gt 0) {
        & python "$PSScriptRoot\mutate_corpus.py" --seed $MutationSeed --count $MutationCount --output "$evidence\mutations"
        if ($LASTEXITCODE -ne 0) { throw "Mutation generation failed" }
        & python "$PSScriptRoot\csri_compare.py" --reference $original --candidate $candidate `
            --cases "$evidence\mutations\cases.json" --output "$evidence\mutation-results" --timeout 10 `
            --environment-label "GitHub windows-2022; VS2022 Release $Platform; mutation seed $MutationSeed"
        if ($LASTEXITCODE -ne 0) { throw "Mutation comparison has failures; preserve and reduce its cases" }
    }
} finally {
    Pop-Location
}
