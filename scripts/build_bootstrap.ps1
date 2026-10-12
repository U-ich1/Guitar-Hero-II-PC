$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $env:PATH = "$projectRoot\tools\llvm-mingw-20261006-ucrt-x86_64\bin;$env:PATH"
    $cmake = Join-Path $projectRoot 'tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe'
    & $cmake -S . -B build/gh2 -G 'MinGW Makefiles' '-DCMAKE_CXX_COMPILER=clang++' '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'
    if ($LASTEXITCODE -ne 0) { throw 'Bootstrap configuration failed' }
    & $cmake --build build/gh2 --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Bootstrap build failed' }
} finally {
    Pop-Location
}
