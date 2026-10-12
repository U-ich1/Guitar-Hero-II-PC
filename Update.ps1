param(
    [Parameter(Mandatory=$true)][string]$SourceArchive,
    [string]$Python = 'python',
    [string]$Git = 'git',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$Offline,
    [switch]$LauncherOnly
)
$ErrorActionPreference = 'Stop'
& $Python (Join-Path $PSScriptRoot 'scripts/update_source.py') $SourceArchive
if ($LASTEXITCODE -ne 0) { throw 'Source update failed; build was not started.' }
$parameters = @{ Python=$Python; Git=$Git; Jobs=$Jobs }
if ($Offline) { $parameters.Offline=$true }
if ($LauncherOnly) { $parameters.LauncherOnly=$true }
& (Join-Path $PSScriptRoot 'Build.ps1') @parameters
