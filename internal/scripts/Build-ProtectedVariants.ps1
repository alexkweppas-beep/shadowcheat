param(
    [Parameter(Mandatory = $true)][string]$VmpRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [int]$Count = 32,
    [string]$Watermark = ""
)

$ErrorActionPreference = "Stop"
$env:PYTHONDONTWRITEBYTECODE = "1"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $sourceRoot "build-variants"

if ($Count -lt 2 -or $Count -gt 64) {
    throw "Count must be between 2 and 64"
}

function New-VariantSeed {
    $bytes = New-Object byte[] 8
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($bytes)
    } finally {
        $generator.Dispose()
    }
    return ($bytes | ForEach-Object { $_.ToString("x2") }) -join ""
}

$featureRoot = Join-Path (Split-Path -Parent $OutputRoot) "features"
& python `
    (Join-Path $PSScriptRoot "generate_script_blobs.py") `
    --farm (Join-Path $sourceRoot "src\Features\FiveRpFarmBotScript.h") `
    --quant (Join-Path $sourceRoot "src\Features\QuantToolsScript.h") `
    --misc (Join-Path $sourceRoot "src\Features\MiscFeatures.cpp") `
    --ukraine (Join-Path $sourceRoot "src\Features\UkraineToolsScript.h") `
    --bundle-dir $featureRoot
if ($LASTEXITCODE -ne 0) {
    throw "Feature bundle export failed"
}

for ($variant = 0; $variant -lt $Count; ++$variant) {
    $id = $variant.ToString("00")
    $seed = New-VariantSeed
    $buildDirectory = Join-Path $buildRoot "work"
    $outputDirectory = Join-Path $OutputRoot $id
    $arguments = @(
        "-S", $sourceRoot,
        "-B", $buildDirectory,
        "-A", "x64",
        "-DSHADOW_VMP_ROOT=$VmpRoot",
        "-DSHADOW_BUILD_TAG=cohort-$id",
        "-DSHADOW_VARIANT_ID=$variant",
        "-DSHADOW_VARIANT_SEED=$seed",
        "-DSHADOW_KEEP_VMP_RAW=OFF",
        "-DSHADOW_BUILD_DEV_INJECTOR=OFF"
    )
    if ($Watermark) {
        $arguments += "-DSHADOW_VMP_WATERMARK=$Watermark"
    }
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Configure failed for variant $id"
    }
    & cmake --build $buildDirectory --config Release --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for variant $id"
    }
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    Copy-Item `
        -LiteralPath (Join-Path $buildDirectory "Release\shadow_internal.dll") `
        -Destination (Join-Path $outputDirectory "cheat.dll") `
        -Force
}

& python `
    (Join-Path $PSScriptRoot "audit_variant_catalog.py") `
    $OutputRoot `
    --count $Count
if ($LASTEXITCODE -ne 0) {
    throw "Variant catalog audit failed"
}
