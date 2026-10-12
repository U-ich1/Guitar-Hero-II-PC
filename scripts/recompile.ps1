param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $env:PATH = "$projectRoot\tools\llvm-mingw-20261006-ucrt-x86_64\bin;$env:PATH"
    New-Item -ItemType Directory -Force generated,build | Out-Null
    & build/xenon/XenonAnalyse/XenonAnalyse.exe private/default.xex config/gh2_switch_tables.toml *> build/analyse.log
    if ($LASTEXITCODE -ne 0) { throw 'XenonAnalyse failed; see build/analyse.log' }
    & $Python scripts/analyze_image.py
    if ($LASTEXITCODE -ne 0) { throw 'Memory image inspection failed' }
    & $Python scripts/prepare_config.py
    if ($LASTEXITCODE -ne 0) { throw 'GH2 configuration validation failed' }
    & build/xenon/XenonRecomp/XenonRecomp.exe config/gh2.toml tools/XenonRecomp/XenonUtils/ppc_context.h *> build/recompile.log
    if ($LASTEXITCODE -ne 0) { throw 'XenonRecomp failed; see build/recompile.log' }
    Copy-Item runtime/gh2_ppc_helpers.h generated/gh2_ppc_helpers.h
    & $Python scripts/prepare_import_stubs.py
    if ($LASTEXITCODE -ne 0) { throw 'Import stub generation failed' }
    & $Python scripts/inspect_imports.py
    if ($LASTEXITCODE -ne 0) { throw 'Import inventory failed' }
    Write-Output 'Translation emitted. Review build/recompile.log: exit code zero does not mean valid game code.'
} finally {
    Pop-Location
}
