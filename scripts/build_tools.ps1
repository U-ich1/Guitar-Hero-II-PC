param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    & $Python scripts/apply_tool_patches.py
    if ($LASTEXITCODE -ne 0) { throw 'Project tool patches failed' }
    $env:PATH = "$projectRoot\tools\llvm-mingw-20261006-ucrt-x86_64\bin;$env:PATH"
    $cmake = Join-Path $projectRoot 'tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe'
    & $cmake -S tools/XenonRecomp -B build/xenon -G 'MinGW Makefiles' '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_C_COMPILER=clang' '-DCMAKE_CXX_COMPILER=clang++' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DCMAKE_CXX_FLAGS=-include cstdlib'
    if ($LASTEXITCODE -ne 0) { throw 'Tool configuration failed' }
    & $cmake --build build/xenon --target XenonAnalyse XenonRecomp --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'Tool build failed' }
} finally {
    Pop-Location
}
