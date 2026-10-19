#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$sourceProcess=Get-Process -Id $PID
$sourcePriority=$sourceProcess.PriorityClass
try {
    $sourceProcess.PriorityClass='BelowNormal'
    & py -3.12 (Join-Path $PSScriptRoot 'source_package.py') build --binary-directory $BinaryDirectory --output $OutputDirectory
    if ($LASTEXITCODE -ne 0) { throw "Source packaging failed: $LASTEXITCODE" }
} finally { $sourceProcess.PriorityClass=$sourcePriority }
