param(
    [Parameter(Mandatory=$true)][string]$SourceArchive,
    [string]$Python = 'python',
    [string]$Git = 'git',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$Offline,
    [switch]$LauncherOnly
)
$ErrorActionPreference = 'Stop'
# Check before replacing sources, rather than waiting for the build preflight.
foreach ($name in @('gh2_pc','Guitar Hero 2','Guitar Hero 2 Launcher','GH2 Launcher')) {
    foreach ($running in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
        if (!$running.Path -or $running.Path.StartsWith($PSScriptRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Close the game and launcher normally before updating. No source files were changed.'
        }
    }
}
& $Python (Join-Path $PSScriptRoot 'scripts/update_source.py') $SourceArchive
if ($LASTEXITCODE -ne 0) { throw 'Source update failed; build was not started.' }
$parameters = @{ Python=$Python; Git=$Git; Jobs=$Jobs }
if ($Offline) { $parameters.Offline=$true }
if ($LauncherOnly) { $parameters.LauncherOnly=$true }
& (Join-Path $PSScriptRoot 'Build.ps1') @parameters
if ($LASTEXITCODE -ne 0) { throw 'Build failed; the update is not complete.' }
