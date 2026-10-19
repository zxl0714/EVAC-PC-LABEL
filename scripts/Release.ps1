#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Internal','Public')][string]$Flavor,
    [string]$Version='0.1',
    [string]$OutputDirectory,
    [hashtable]$DataDirectories=@{},
    [string]$FutariDlcDirectory
)
$ErrorActionPreference='Stop'
$releaseProcess=Get-Process -Id $PID
$releasePriority=$releaseProcess.PriorityClass
try {
    $releaseProcess.PriorityClass='BelowNormal'
    $releaseArgs=@('-3.12',(Join-Path $PSScriptRoot 'release_package.py'),'build','--flavor',$Flavor,'--version',$Version)
    if ($OutputDirectory) { $releaseArgs+=@('--output',$OutputDirectory) }
    foreach ($game in @($DataDirectories.Keys | Sort-Object)) { $releaseArgs+=@('--data-dir',"$game=$($DataDirectories[$game])") }
    if ($FutariDlcDirectory) { $releaseArgs+=@('--futari-dlc',$FutariDlcDirectory) }
    & py @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "Release packaging failed: $LASTEXITCODE" }
} finally { $releaseProcess.PriorityClass=$releasePriority }
