$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$ObsBuild = Join-Path $ProjectRoot "build\obs"
$RunDir = Join-Path $ObsBuild "rundir\RelWithDebInfo"
$BinDir = Join-Path $RunDir "bin\64bit"

$DepsDir = Join-Path $ProjectRoot ".deps"
$DepsVersion = "2025-08-23"
$QtBinDir = Join-Path $DepsDir "obs-deps-qt6-$DepsVersion-x64\bin"

$exe = Join-Path $BinDir "watchwith.exe"
if (-not (Test-Path $exe)) {
    Write-Error "watchwith.exe not found at $exe`nRun scripts\build-windows.ps1 first."
    exit 1
}

Stop-Process -Name watchwith -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$env:PATH = "$BinDir;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = $QtBinDir

Start-Process -FilePath $exe -WorkingDirectory $RunDir
Write-Host "WatchWith launched." -ForegroundColor Green
