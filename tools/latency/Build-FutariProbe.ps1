#requires -Version 7.0
[CmdletBinding()]
param([string]$FixtureDirectory='.local/input-latency-v080-r1/evac-futari-probe-r1')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$fixture=[IO.Path]::GetFullPath($FixtureDirectory)
$build=Join-Path $fixture 'build'
$cmake=Get-CollectionTool 'cmake'
$arguments=@('-S',$root,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    ('-DCMAKE_CXX_COMPILER='+(Get-CollectionTool 'clang++')),
    ('-DCMAKE_C_COMPILER='+(Get-CollectionTool 'clang')),
    ('-DCMAKE_MAKE_PROGRAM='+(Get-CollectionTool 'ninja')),
    '-DCAVE_GAMES=futari','-DCAVE_BUILD_GAME=ON',
    ('-DCMAKE_PREFIX_PATH='+(Join-Path $root '.deps/sdk-install')),
    '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG -g -gcodeview',
    '-DCMAKE_EXE_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF',
    '-DCMAKE_SHARED_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF',
    ('-DEVAC_FUTARI_PROBE_DIRECTORY='+$fixture),
    ('-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES='+(Join-Path $PSScriptRoot 'futari_probe.cmake')))
Invoke-CollectionConfigure $cmake $arguments $build
Invoke-Checked $cmake @('--build',$build,'--target','futari','futari_reco_abnormal','--parallel','2')
