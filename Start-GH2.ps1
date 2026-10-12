$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$outputRoot = Join-Path $projectRoot 'tools/rexglue-sdk/out/win-amd64'
$executable = Join-Path $outputRoot 'Guitar Hero 2.exe'
if (!(Test-Path -LiteralPath $executable) -and (Test-Path -LiteralPath (Join-Path $outputRoot 'gh2_pc.exe'))) {
    if (Get-Process -Name gh2_pc,'Guitar Hero 2' -ErrorAction SilentlyContinue) { throw 'Close GH2 normally before migrating its executable name.' }
    Move-Item -LiteralPath (Join-Path $outputRoot 'gh2_pc.exe') -Destination $executable
}
$updates = @(
    @{ Pending = 'Guitar Hero 2.next.exe'; Active = 'Guitar Hero 2.exe'; Backup = 'Guitar Hero 2.previous.exe' },
    @{ Pending = 'librexruntime.next.dll'; Active = 'librexruntime.dll'; Backup = 'librexruntime.previous.dll' },
    @{ Pending = 'rexgpu-xenos.next.dll'; Active = 'rexgpu-xenos.dll'; Backup = 'rexgpu-xenos.previous.dll' }
)
foreach ($update in $updates) {
    $pendingUpdate = Join-Path $outputRoot $update.Pending
    if (!(Test-Path -LiteralPath $pendingUpdate)) { continue }
    if (Get-Process -Name gh2_pc,'Guitar Hero 2' -ErrorAction SilentlyContinue) { throw 'Close GH2 normally before applying the prepared update.' }
    $activeUpdate = Join-Path $outputRoot $update.Active
    if (Test-Path -LiteralPath $activeUpdate) {
        Copy-Item -LiteralPath $activeUpdate -Destination (Join-Path $outputRoot $update.Backup) -Force
    }
    Copy-Item -LiteralPath $pendingUpdate -Destination $activeUpdate -Force
    Remove-Item -LiteralPath $pendingUpdate
}
if (!(Test-Path -LiteralPath $executable)) { throw 'Build the windowed target with scripts/build_gpu_backend.ps1 -Windowed first.' }
$env:PATH = "$projectRoot/tools/llvm-mingw-20261006-ucrt-x86_64/bin;$env:PATH"
$stateRoot = Join-Path $projectRoot 'build/windowed-state'
New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null
$logPath = Join-Path $stateRoot 'gh2.log'
Start-Process -FilePath $executable -WindowStyle Hidden -Wait -ArgumentList @('--input_backend=xinput', '--mnk_mode=true', '--keybind_dpad_up=Up', '--keybind_dpad_down=Down', '--keybind_dpad_left=Left', '--keybind_dpad_right=Right', '--log_level=info', ('"--log_file={0}"' -f $logPath))
