# Stage the built Standalone + VST3 (with their ORT DLLs + model) and compile the
# Inno Setup installer. Windows x64, per-machine.
#
# Prereqs:
#   - Inno Setup 6.3+ installed (provides ISCC.exe): https://jrsoftware.org/isdl.php
#   - The plugin built with the model present, so onnxruntime.dll, DirectML.dll,
#     and runtime_model.onnx are staged next to each binary:
#         cmake --build cmake-build-release --target BuildAll --config Release
#
# Usage (from cpp_impl\installer):
#   .\build_installer.ps1                         # ..\cmake-build-release, Release
#   .\build_installer.ps1 -BuildDir ..\build      # custom CMake build dir
#
# Output: .\Output\Linkjiru-Setup-<version>.exe

param(
    [string]$BuildDir = "..\cmake-build-release",
    [string]$Config   = "Release"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

$vst3Src       = Join-Path $here "..\artifacts\Linkjiru.vst3"
$standaloneSrc = Join-Path $BuildDir "Linkjiru_artefacts\$Config\Standalone"

# --- sanity: built artifacts + their runtime deps must exist ---
if (-not (Test-Path $vst3Src)) {
    throw "VST3 bundle not found: $vst3Src`nBuild the plugin first: cmake --build $BuildDir --target BuildAll --config $Config"
}
if (-not (Test-Path $standaloneSrc)) {
    throw "Standalone build not found: $standaloneSrc"
}
foreach ($f in @("onnxruntime.dll", "DirectML.dll", "runtime_model.onnx")) {
    if (-not (Test-Path (Join-Path $standaloneSrc $f))) {
        throw "Missing runtime dependency next to the Standalone: $f`nRebuild with the model present at cpp_impl\onnx-model\runtime_model.onnx"
    }
}

# --- stage a clean tree the .iss sources from ---
$staging = Join-Path $here "staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $staging "VST3") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $staging "Standalone") | Out-Null

Copy-Item $vst3Src (Join-Path $staging "VST3\Linkjiru.vst3") -Recurse
Copy-Item (Join-Path $standaloneSrc "*") (Join-Path $staging "Standalone") -Recurse

# --- app-local VC++ runtime (VCRUNTIME140/MSVCP140), so it runs on machines
#     that don't have the VC++ redistributable installed. Located from the VS
#     redist folder; shipped next to both binaries (Microsoft permits this). ---
$crtDir = $null
if ($env:VCToolsRedistDir) {
    $crtDir = (Get-ChildItem (Join-Path $env:VCToolsRedistDir "x64") -Filter "Microsoft.VC*.CRT" `
               -Directory -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $crtDir) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        if ($vsPath) {
            $crtDir = (Get-ChildItem (Join-Path $vsPath "VC\Redist\MSVC") -Directory -ErrorAction SilentlyContinue |
                       Sort-Object Name -Descending |
                       ForEach-Object { Get-ChildItem (Join-Path $_.FullName "x64") -Filter "Microsoft.VC*.CRT" `
                                        -Directory -ErrorAction SilentlyContinue } |
                       Select-Object -First 1).FullName
        }
    }
}
if (-not $crtDir -or -not (Test-Path (Join-Path $crtDir "vcruntime140.dll"))) {
    throw "Couldn't find the VC++ redist CRT folder (Microsoft.VC*.CRT). Run from a Visual Studio developer prompt, or install the VC++ redistributable."
}
$vst3Win = Join-Path $staging "VST3\Linkjiru.vst3\Contents\x86_64-win"
Copy-Item (Join-Path $crtDir "*.dll") (Join-Path $staging "Standalone") -Force
Copy-Item (Join-Path $crtDir "*.dll") $vst3Win -Force
Write-Host "Bundled VC++ runtime from: $crtDir"

# --- locate the Inno Setup compiler ---
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                     "$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) {
    throw "ISCC.exe (Inno Setup compiler) not found. Install Inno Setup 6 from https://jrsoftware.org/isdl.php"
}

# --- compile (Output\ and staging\ are relative to this dir) ---
& $iscc "Linkjiru.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }

Write-Host "`nInstaller written to: $(Join-Path $here 'Output')" -ForegroundColor Green
