param([switch]$InstallToSteamVR)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-win64'
cmake -S $root -B $build -A x64 -DSVRT_BUILD_PI_LIBRARY=OFF -DSVRT_BUILD_RECEIVER=OFF
cmake --build $build --config Release
$package = Join-Path $build 'svrt'

$registry = Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
if (-not (Test-Path -LiteralPath $registry)) { throw "SteamVR vrpathreg.exe not found: $registry" }

$driverPath = $package
if ($InstallToSteamVR) {
  $steamVrRoot = Split-Path (Split-Path (Split-Path $registry -Parent) -Parent) -Parent
  $target = Join-Path $steamVrRoot 'drivers\svrt'
  New-Item -ItemType Directory -Path $target -Force | Out-Null
  $duplicate = Join-Path $target 'resources\resources'
  if (Test-Path -LiteralPath $duplicate) {
    Remove-Item -LiteralPath $duplicate -Recurse -Force
  }
  Copy-Item -Path (Join-Path $package '*') -Destination $target -Recurse -Force
  $driverPath = $target
  Write-Host "SVRT driver installed at $target"
}

& $registry adddriver $driverPath
if ($LASTEXITCODE -ne 0) { throw "vrpathreg failed with exit code $LASTEXITCODE" }

Write-Host "SVRT driver packaged at $package"
