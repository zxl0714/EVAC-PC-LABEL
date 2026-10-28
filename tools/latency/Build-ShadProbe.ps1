#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Fixture,
      [Parameter(Mandatory)][string]$Destination,
      [switch]$ResumeLink)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$arguments=@('-3.12','tools/latency/rebuild_shad_probe.py',
    '--fixture',$Fixture,'--destination',$Destination,'--execute')
if ($ResumeLink) { $arguments+='--resume-link' }
Invoke-Checked (Get-Command py).Source $arguments
