param([string]$OutputName = 'Guitar Hero 2 Launcher.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $projectRoot 'tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
$resourceCompiler = Join-Path $projectRoot 'tools/llvm-mingw-20261006-ucrt-x86_64/bin/llvm-rc.exe'
$resource = Join-Path $projectRoot 'build/launcher.res'
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'launcher/assets/gh2.ico'))) { throw 'Run scripts/extract_launcher_icon.py against the local game image before building.' }
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'launcher/assets/gh2-pc-logo.png'))) { throw 'Custom launcher logo artwork is missing.' }
Push-Location (Join-Path $projectRoot 'launcher')
try { & $resourceCompiler /fo $resource launcher.rc; if ($LASTEXITCODE -ne 0) { throw 'Icon resource build failed' } } finally { Pop-Location }
& $compiler -std=c++23 -O2 -static -mwindows -municode -DNOMINMAX -DWIN32_LEAN_AND_MEAN '-D_WIN32_WINNT=0x0A00' '-DNTDDI_VERSION=0x0A000000' (Join-Path $projectRoot 'launcher/main.cpp') $resource ('-I' + (Join-Path $projectRoot 'tools/rexglue-sdk/thirdparty/tomlplusplus')) -lcomctl32 -luxtheme -ldinput8 -ldxguid -lole32 -lshell32 -luuid -lmsimg32 -lwindowscodecs -o (Join-Path $projectRoot $OutputName)
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed' }

