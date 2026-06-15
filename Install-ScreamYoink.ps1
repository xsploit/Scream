param(
    [string]$Source = "$PSScriptRoot\build-yoink\Release\Scream.vst3",
    [string]$Target = "C:\Program Files\Common Files\VST3\Scream.vst3",
    [switch]$ForceReplace
)

$ErrorActionPreference = "Stop"

$resolvedSource = (Resolve-Path -LiteralPath $Source).Path
$targetLeaf = Split-Path -Leaf $Target
if ($targetLeaf -ne "Scream.vst3") {
    throw "Refusing to install to unexpected target bundle: $Target"
}

$targetParent = Split-Path -Parent $Target
New-Item -ItemType Directory -Force -Path $targetParent | Out-Null

if (Test-Path -LiteralPath $Target) {
    if (-not $ForceReplace) {
        throw "Target already exists. Re-run with -ForceReplace to replace only this exact bundle: $Target"
    }
    Remove-Item -LiteralPath $Target -Recurse -Force
}

Copy-Item -LiteralPath $resolvedSource -Destination $targetParent -Recurse -Force

$installedDll = Join-Path $Target "Contents\x86_64-win\Scream.vst3"
$installedInfo = Join-Path $Target "Contents\Resources\moduleinfo.json"

[pscustomobject]@{
    Source = $resolvedSource
    Target = $Target
    DllBytes = (Get-Item -LiteralPath $installedDll).Length
    ModuleInfo = (Get-Item -LiteralPath $installedInfo).FullName
} | Format-List
