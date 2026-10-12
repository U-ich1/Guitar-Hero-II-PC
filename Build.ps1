param(
    [string]$Dump,
    [string]$Python = 'python',
    [string]$Git = 'git',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$Check,
    [switch]$Offline,
    [switch]$LauncherOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if (!$Check) {
    foreach ($name in @('gh2_pc', 'Guitar Hero 2 Launcher', 'GH2 Launcher')) {
        foreach ($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            if ($process.Path -and $process.Path.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Close the game and launcher normally before building this project. No process was stopped.'
            }
        }
    }
}
$arguments = @((Join-Path $projectRoot 'scripts/build_project.py'), '--git', $Git, '--jobs', $Jobs)
if ($Dump) { $arguments += @('--dump', $Dump) }
if ($Check) { $arguments += '--check' }
if ($Offline) { $arguments += '--offline' }
if ($LauncherOnly) { $arguments += '--launcher-only' }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Build did not complete. See the error above; existing saves were not modified.' }
