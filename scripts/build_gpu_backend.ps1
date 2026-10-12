param([int]$Jobs = 4, [string]$Python = 'python', [switch]$GameRuntime, [switch]$Windowed)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $env:PATH = "$projectRoot\tools\llvm-mingw-20261006-ucrt-x86_64\bin;$env:PATH"
    $cmake = "$projectRoot\tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe"
    if (!(Test-Path tools/rexglue-sdk/CMakeLists.txt)) { throw 'Download the pinned SDK and dependencies described in docs/graphics_next.md first.' }
    if ($Windowed) { & $Python scripts/extract_launcher_icon.py; if ($LASTEXITCODE -ne 0) { throw 'GH2 icon extraction failed' } }
    & $Python scripts/apply_rexglue_patches.py
    if ($LASTEXITCODE -ne 0) { throw 'GPU SDK compatibility patch failed' }
    & $Python scripts/prepare_rex_runtime.py
    if ($LASTEXITCODE -ne 0) { throw 'SDK-context translation preparation failed' }
    & $cmake -S tools/rexglue-sdk -B build/rexglue -G 'MinGW Makefiles' '-DCMAKE_C_COMPILER=clang.exe' '-DCMAKE_CXX_COMPILER=clang++.exe' '-DCMAKE_BUILD_TYPE=Release' '-DREXGLUE_ENABLE_TRACY=OFF' '-DREXGLUE_ENABLE_PERF_COUNTERS=OFF' '-DREXGLUE_ENABLE_FIDELITYFX=OFF' '-DREXGLUE_BUILD_TESTS=OFF' '-DREXGLUE_USE_D3D12=ON' '-DREXGLUE_USE_VULKAN=OFF'
    if ($LASTEXITCODE -ne 0) { throw 'GPU SDK configuration failed' }
    $target = if ($Windowed) { 'gh2_pc' } elseif ($GameRuntime) { 'gh2_rex_bootstrap' } else { 'gh2_rex_gpu_smoke' }
    & $cmake --build build/rexglue --target $target --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'GPU SDK build failed; this experimental MinGW build may require compatibility patches.' }
    foreach ($runtimeDll in @('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll')) {
        Copy-Item -LiteralPath "$projectRoot/tools/llvm-mingw-20261006-ucrt-x86_64/bin/$runtimeDll" -Destination "$projectRoot/tools/rexglue-sdk/out/win-amd64/$runtimeDll"
    }
} finally { Pop-Location }
