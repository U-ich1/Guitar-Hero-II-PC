param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $env:PATH = "$projectRoot\tools\llvm-mingw-20261006-ucrt-x86_64\bin;$env:PATH"
    & $Python tests/run_instruction_tests.py
    if ($LASTEXITCODE -ne 0) { throw 'Instruction validation failed' }
    & tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe '-std=c++20' tests/function_analysis.cpp build/xenon/XenonAnalyse/libLibXenonAnalyse.a build/xenon/XenonUtils/libXenonUtils.a build/xenon/thirdparty/disasm/libdisasm.a -I tools/XenonRecomp/XenonAnalyse -I tools/XenonRecomp/XenonUtils -I tools/XenonRecomp/thirdparty/disasm -o build/function_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Function regression build failed' }
    & build/function_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Function regressions failed' }
    & tools/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe '-std=c++20' '-msse4.1' -mavx -static tests/kernel_runtime.cpp runtime/kernel_runtime.cpp runtime/game_files.cpp runtime/guest_clock.cpp -I generated -I runtime -I tools/XenonRecomp/thirdparty/simde -o build/kernel_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Kernel regression build failed' }
    & build/kernel_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Kernel regressions failed' }
    & $Python scripts/inspect_imports.py
    if ($LASTEXITCODE -ne 0) { throw 'Import inventory failed' }
    & $Python scripts/validate_generated.py
    if ($LASTEXITCODE -ne 0) { throw 'Generated code validation failed' }
    & $Python tests/check_startup_trace.py
    if ($LASTEXITCODE -ne 0) { throw 'Startup trace validation failed' }
} finally {
    Pop-Location
}
