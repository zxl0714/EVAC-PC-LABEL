#requires -Version 7.0
[CmdletBinding()]
param([string]$Destination='.local/input-latency-v080-r1/evac-sdoj-build-r1')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $taskRoot
. (Join-Path $taskRoot 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
Invoke-Checked 'py' @('-3.12','tools/latency/build_sdoj_host_probe.py','--destination',$Destination)
