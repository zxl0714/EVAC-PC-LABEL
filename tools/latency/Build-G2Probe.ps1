#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$FixtureDirectory='.local/input-latency-v080-r1/evac-g2-probe-r1',
    [string]$Destination='.local/input-latency-v080-r1/evac-g2-build-r1',
    [string]$Reuse=''
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $taskRoot
. (Join-Path $taskRoot 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$taskArgs=@('-3.12','tools/latency/rebuild_g2_probe.py',
    '--fixture',$FixtureDirectory,'--destination',$Destination)
if($Reuse) {$taskArgs+=@('--reuse',$Reuse)}
Invoke-Checked 'py' $taskArgs
