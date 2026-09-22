param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [Parameter(Mandatory = $true)][string]$VmpRoot,
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [string]$Watermark = ""
)

$ErrorActionPreference = "Stop"

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label does not exist: $Path"
    }
}

$console = Join-Path $VmpRoot "VMProtect_Con.exe"
Require-File $InputFile "Raw loader"
Require-File $console "VMProtect console"
Require-File $ProjectFile "VMProtect project"

$outputDirectory = Split-Path -Parent $OutputFile
$temporaryOutput = Join-Path $outputDirectory "loader.vmp.tmp.exe"
$localProject = Join-Path $outputDirectory "loader.build.vmp"

Remove-Item $OutputFile, $temporaryOutput, $localProject -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $ProjectFile -Destination $localProject

$arguments = @(
    (Split-Path -Leaf $InputFile),
    (Split-Path -Leaf $temporaryOutput),
    "-pf",
    (Split-Path -Leaf $localProject)
)
if ($Watermark) {
    $arguments += @("-wm", $Watermark)
}
$arguments += "-we"

$vmpExitCode = 1
Push-Location $outputDirectory
try {
    & $console @arguments
    $vmpExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($vmpExitCode -ne 0 -or -not (Test-Path -LiteralPath $temporaryOutput)) {
    Remove-Item $temporaryOutput, $localProject, $OutputFile -Force -ErrorAction SilentlyContinue
    throw "VMProtect failed with exit code $vmpExitCode"
}

$ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($temporaryOutput))
if ($ascii.Contains("VMProtectSDK64.dll")) {
    Remove-Item $temporaryOutput, $localProject, $OutputFile -Force -ErrorAction SilentlyContinue
    throw "Protected loader still imports VMProtectSDK64.dll"
}

Move-Item -LiteralPath $temporaryOutput -Destination $OutputFile
Remove-Item $InputFile, $localProject -Force
Get-ChildItem -LiteralPath $outputDirectory -Filter "*.pdb" |
    Remove-Item -Force -ErrorAction SilentlyContinue

$auditScript = Join-Path $PSScriptRoot "audit_protected_loader.py"
Require-File $auditScript "Loader artifact audit"
& python $auditScript $OutputFile
if ($LASTEXITCODE -ne 0) {
    Remove-Item $OutputFile -Force -ErrorAction SilentlyContinue
    throw "Protected loader artifact audit failed"
}

Write-Host "Protected loader: $OutputFile"
