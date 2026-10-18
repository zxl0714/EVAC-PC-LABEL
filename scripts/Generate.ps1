#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('espgaluda2')][string]$Game='espgaluda2',
    [Parameter(Mandatory)][string]$DataDirectory,
    [switch]$Regenerate
)
$ErrorActionPreference='Stop'
$taskProcess=Get-Process -Id $PID
$previousPriority=$taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass='BelowNormal'
    $arguments=@('-3.12',(Join-Path $PSScriptRoot 'generate_game.py'),'--game',$Game,'--data-dir',$DataDirectory)
    if ($Regenerate) { $arguments+='--regenerate' }
    & py @arguments
    if ($LASTEXITCODE -ne 0) { throw "Game code generation failed: $LASTEXITCODE" }
} finally { $taskProcess.PriorityClass=$previousPriority }
