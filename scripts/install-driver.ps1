$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-win64'
cmake -S $root -B $build -A x64 -DSVRT_BUILD_PI_LIBRARY=OFF -DSVRT_BUILD_RECEIVER=OFF
cmake --build $build --config Release
$registry = Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
if (-not (Test-Path -LiteralPath $registry)) { throw "SteamVR vrpathreg.exe not found: $registry" }
& $registry adddriver (Join-Path $build 'svrt')
if ($LASTEXITCODE -ne 0) { throw "vrpathreg failed with exit code $LASTEXITCODE" }
Write-Host "SVRT driver registered from $(Join-Path $build 'svrt')"
