#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2, [string]$CacheFrom, [switch]$ToolsOnly,
      [string]$ProbeGenerated, [string]$ArcadeGenerated, [string]$ArrangeGenerated,
      [string]$StageModulesManifest)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskProcess=Get-Process -Id $PID
$previousPriority=$taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass='BelowNormal'
    if($ToolsOnly -and $ProbeGenerated) { throw '-ToolsOnly cannot compile the development host' }
    if(($ArcadeGenerated -or $ArrangeGenerated) -and
       (-not $ProbeGenerated -or -not $ArcadeGenerated -or -not $ArrangeGenerated)) {
        throw 'Both ESPGaluda modules and the development host are required together'
    }
    if($StageModulesManifest -and (-not $ArcadeGenerated -or -not $ArrangeGenerated)) {
        throw 'Stage modules require both ESPGaluda mode modules'
    }
    Initialize-CollectionBuildEnvironment
    $restore=@('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
    if ($CacheFrom) { $restore+=@('--cache-from',$CacheFrom) }
    Invoke-Checked 'py' $restore
    $cmake=Get-CollectionTool 'cmake'
    $build=Join-Path $CollectionRoot 'out/ps2-tools'
    $configure=@('-S',(Join-Path $CollectionRoot 'tools/ps2'),'-B',$build,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        ('-DCMAKE_C_COMPILER='+(Get-CollectionTool 'clang-cl')),
        ('-DCMAKE_CXX_COMPILER='+(Get-CollectionTool 'clang-cl')),
        ('-DCMAKE_MAKE_PROGRAM='+(Get-CollectionTool 'ninja')),
        '-DCMAKE_C_FLAGS_RELEASE=/O2 /DNDEBUG /Zi',
        '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Zi /EHsc /fp:precise',
        '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG:FULL /OPT:REF /OPT:ICF',
        ('-DPS2_BUILD_RUNTIME='+$(if ($ToolsOnly) {'OFF'} else {'ON'})))
    $configure+=@('-DPS2_GAME_GENERATED='+$(if ($ProbeGenerated) {[IO.Path]::GetFullPath($ProbeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_ARCADE_GENERATED='+$(if ($ArcadeGenerated) {[IO.Path]::GetFullPath($ArcadeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_ARRANGE_GENERATED='+$(if ($ArrangeGenerated) {[IO.Path]::GetFullPath($ArrangeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_STAGE_MODULES_MANIFEST='+$(if ($StageModulesManifest) {[IO.Path]::GetFullPath($StageModulesManifest).Replace('\','/')} else {''}))
    Invoke-CollectionConfigure $cmake $configure $build
    $targets=@('--build',$build,'--parallel',"$Jobs",'--target','ps2_recomp','ps2_analyzer','ps2_vu_contract','ps2_module_contract','ps2_pad_contract')
    if (-not $ToolsOnly) { $targets+=@('ps2_runtime','ps2_sdk_contract','ps2_kernel_contract') }
    if ($ProbeGenerated) { $targets+='ps2_espgaluda_probe' }
    Invoke-Checked $cmake $targets
    Write-Host "PS2 tools and requested development targets built under $build. No game has been launched."
} finally { $taskProcess.PriorityClass=$previousPriority }
