#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputPackage,
    [Parameter(Mandatory)][string]$OutputDir
)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Extract-Xbla.ps1') -InputPackage $InputPackage -OutputDir $OutputDir -ContentKind Dlc
