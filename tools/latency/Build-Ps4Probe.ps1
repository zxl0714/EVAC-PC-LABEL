#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Fixture,
      [Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
Invoke-Checked (Get-Command py).Source @('-3.12','tools/latency/rebuild_ps4_probe.py',
    '--fixture',$Fixture,'--destination',$Destination,'--execute')
