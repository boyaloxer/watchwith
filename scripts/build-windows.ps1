$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$ObsSrc = Join-Path $ProjectRoot "deps\obs-studio"
$ObsBuild = Join-Path $ProjectRoot "build\obs"
$WatchWithBuild = Join-Path $ProjectRoot "build\watchwith"

$DepsVersion = "2025-08-23"
$DepsBaseUrl = "https://github.com/obsproject/obs-deps/releases/download"
$DepsDir = Join-Path $ProjectRoot ".deps"

# Find cmake
$vsCmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $vsCmake)) {
    $vsCmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $vsCmake) {
        Write-Error "CMake not found. Install Visual Studio 2022 with 'Desktop development with C++' workload."
        exit 1
    }
}

function Download-And-Extract {
    param([string]$Url, [string]$DestDir, [string]$Label)

    if (Test-Path (Join-Path $DestDir "include")) {
        Write-Host "  [skip] $Label already downloaded" -ForegroundColor DarkGray
        return
    }

    $zipFile = Join-Path $env:TEMP "$Label.zip"
    Write-Host "  Downloading $Label..." -ForegroundColor Cyan
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    (New-Object Net.WebClient).DownloadFile($Url, $zipFile)

    Write-Host "  Extracting to $DestDir..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    Expand-Archive -Path $zipFile -DestinationPath $DestDir -Force
    Remove-Item $zipFile
}

Write-Host "`n=== WatchWith Build Script (Windows x64) ===" -ForegroundColor Green

# Step 1: Init submodules
Write-Host "`n[1/5] Initializing submodules..." -ForegroundColor Yellow
Push-Location $ProjectRoot
git submodule update --init --depth 1 deps/obs-studio 2>$null
git submodule update --init --depth 1 deps/libdatachannel 2>$null
git -C deps/libdatachannel submodule update --init --depth 1 2>$null
# OBS's own submodules (libdshowcapture needs recursive init)
git -C deps/obs-studio submodule update --init --recursive --depth 1 -- deps/libdshowcapture 2>$null
git -C deps/obs-studio/deps/libdshowcapture/src checkout HEAD 2>$null
git -C deps/obs-studio/deps/libdshowcapture/src submodule update --init --depth 1 2>$null
Pop-Location

# Step 2: Download OBS prebuilt deps
Write-Host "`n[2/5] Downloading OBS dependencies..." -ForegroundColor Yellow

$obsDepsDir = Join-Path $DepsDir "obs-deps-$DepsVersion-x64"
$qtDepsDir = Join-Path $DepsDir "obs-deps-qt6-$DepsVersion-x64"

Download-And-Extract "$DepsBaseUrl/$DepsVersion/windows-deps-$DepsVersion-x64.zip" $obsDepsDir "obs-deps"
Download-And-Extract "$DepsBaseUrl/$DepsVersion/windows-deps-qt6-$DepsVersion-x64.zip" $qtDepsDir "obs-deps-qt6"

$prefixPath = "$obsDepsDir;$qtDepsDir"

# Step 3: Prepare OBS source
Write-Host "`n[3/5] Preparing OBS source..." -ForegroundColor Yellow

# Create stubs for missing OBS submodules (browser, websocket)
foreach ($sub in @("obs-browser", "obs-websocket")) {
    $stubDir = Join-Path $ObsSrc "plugins\$sub"
    $stubFile = Join-Path $stubDir "CMakeLists.txt"
    if (-not (Test-Path $stubFile)) {
        New-Item -ItemType Directory -Path $stubDir -Force | Out-Null
        @"
# Auto-generated stub
add_custom_target($sub)
if(COMMAND target_disable)
  target_disable($sub)
endif()
"@ | Set-Content $stubFile
        Write-Host "  Created stub for $sub" -ForegroundColor DarkGray
    }
}

# Ensure OBS has version tags for git describe
Push-Location $ObsSrc
$hasTag = git describe --tags --abbrev=0 2>$null
if (-not $hasTag) {
    Write-Host "  Fetching OBS version tags..." -ForegroundColor DarkGray
    git fetch --deepen=100 origin 2>$null
    git fetch --tags origin 2>$null
}
Pop-Location

# Step 4: Build OBS
Write-Host "`n[4/5] Building OBS (libobs + plugins)..." -ForegroundColor Yellow

if (-not (Test-Path (Join-Path $ObsBuild "libobs\RelWithDebInfo\obs.lib"))) {
    & $vsCmake -S $ObsSrc -B $ObsBuild `
        -G "Visual Studio 17 2022" -A x64 `
        "-DCMAKE_PREFIX_PATH=$prefixPath" `
        -DENABLE_UI=OFF -DENABLE_SCRIPTING=OFF

    if ($LASTEXITCODE -ne 0) { Write-Error "OBS CMake configure failed"; exit 1 }

    & $vsCmake --build $ObsBuild --config RelWithDebInfo -- /m

    if ($LASTEXITCODE -ne 0) { Write-Error "OBS build failed"; exit 1 }
} else {
    Write-Host "  [skip] OBS already built" -ForegroundColor DarkGray
}

# Step 5: Build WatchWith
Write-Host "`n[5/5] Building WatchWith..." -ForegroundColor Yellow

& $vsCmake -S $ProjectRoot -B $WatchWithBuild `
    -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=$prefixPath" `
    "-DOBS_BUILDDIR=$ObsBuild"

if ($LASTEXITCODE -ne 0) { Write-Error "WatchWith CMake configure failed"; exit 1 }

& $vsCmake --build $WatchWithBuild --config RelWithDebInfo --target watchwith -- /m

if ($LASTEXITCODE -ne 0) { Write-Error "WatchWith build failed"; exit 1 }

$exePath = Join-Path $ObsBuild "rundir\RelWithDebInfo\bin\64bit\watchwith.exe"
Write-Host "`n=== Build complete! ===" -ForegroundColor Green
Write-Host "Executable: $exePath" -ForegroundColor Cyan
Write-Host "`nTo run: .\scripts\run-windows.ps1`n" -ForegroundColor DarkGray
