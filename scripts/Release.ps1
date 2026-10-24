#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Internal','Public')][string]$Flavor='Public',
    [string]$Version='0.6.1',
    [string]$OutputDirectory,
    [hashtable]$DataDirectories=@{},
    [string]$FutariDlcDirectory,
    [ValidateSet('all','futari','espgaluda2','ketsui','sdoj','daioujou','esprade','guwange')][string[]]$DataGames,
    [ValidateSet('all','black-label','ver101')][string[]]$FutariContent,
    [switch]$Symbols
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
    foreach ($game in $DataGames) { $releaseArgs+=@('--data-game',$game) }
    foreach ($content in $FutariContent) { $releaseArgs+=@('--futari-content',$content) }
    if ($Symbols) { $releaseArgs+='--symbols' }
    & py @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "Release packaging failed: $LASTEXITCODE" }
} finally { $releaseProcess.PriorityClass=$releasePriority }
