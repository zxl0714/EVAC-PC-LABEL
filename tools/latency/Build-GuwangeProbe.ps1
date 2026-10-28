#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$FixtureDirectory='.local/input-latency-v080-r1/evac-guwange-probe-r1',
    [string]$Destination='.local/input-latency-v080-r1/evac-guwange-build-r1'
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $taskRoot
. (Join-Path $taskRoot 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$taskBuilder=Join-Path $FixtureDirectory 'rebuild.py'
Invoke-Checked 'py' @('-3.12',$taskBuilder,'--fixture',$FixtureDirectory,'--destination',$Destination)
