param([string]$OutputName = 'Guitar Hero 2 Updater.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $projectRoot 'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
$resourceCompiler = Join-Path $projectRoot 'tools/llvm-mingw-20261006-ucrt-x86_64/bin/llvm-rc.exe'
$resource = Join-Path $projectRoot 'build/updater.res'
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'build') -Force | Out-Null
Push-Location (Join-Path $projectRoot 'launcher')
try {
    & $resourceCompiler /fo $resource updater.rc
    if ($LASTEXITCODE -ne 0) { throw 'Updater resource build failed' }
} finally { Pop-Location }
& $compiler -std=c++23 -O2 -static -mwindows -municode -DNOMINMAX -DWIN32_LEAN_AND_MEAN (Join-Path $projectRoot 'launcher/updater.cpp') $resource -o (Join-Path $projectRoot $OutputName)
if ($LASTEXITCODE -ne 0) { throw 'Updater build failed' }
