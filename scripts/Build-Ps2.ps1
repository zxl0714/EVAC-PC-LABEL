#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2, [string]$CacheFrom, [switch]$ToolsOnly,
      [string]$ProbeGenerated, [string]$ArcadeGenerated, [string]$ArrangeGenerated,
      [string]$IbaraGenerated,
      [string]$StageModulesManifest, [string]$ModeExtrasManifest, [switch]$RuntimeChecks,
      [switch]$FastGameLink)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskProcess=Get-Process -Id $PID
$previousPriority=$taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass='BelowNormal'
    if($RuntimeChecks -and ($ToolsOnly -or $ProbeGenerated -or $ArcadeGenerated -or $ArrangeGenerated -or $IbaraGenerated -or $StageModulesManifest -or $ModeExtrasManifest)) { throw '-RuntimeChecks builds the independent no-ROM check graph' }
    if($ToolsOnly -and ($ProbeGenerated -or $IbaraGenerated)) { throw '-ToolsOnly cannot compile the development host' }
    if(($ArcadeGenerated -or $ArrangeGenerated) -and
       (-not $ProbeGenerated -or -not $ArcadeGenerated -or -not $ArrangeGenerated)) {
        throw 'Both ESPGaluda modules and the development host are required together'
    }
    if($StageModulesManifest -and (-not $ArcadeGenerated -or -not $ArrangeGenerated)) {
        throw 'Stage modules require both ESPGaluda mode modules'
    }
    if($ModeExtrasManifest -and (-not $ArcadeGenerated -or -not $ArrangeGenerated)) {
        throw 'Additional mode entries require both ESPGaluda mode modules'
    }
    Initialize-CollectionBuildEnvironment
    $restore=@('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
    if ($CacheFrom) { $restore+=@('--cache-from',$CacheFrom) }
    Invoke-Checked 'py' $restore
    $cmake=Get-CollectionTool 'cmake'
    $build=Join-Path $CollectionRoot $(if($RuntimeChecks){'out/ps2-native-check'}else{'out/ps2-tools'})
    $gsSource=''
    if (-not $ToolsOnly) {
        $gsSource=& py -3.12 (Join-Path $PSScriptRoot 'ps2_gs_sources.py') --output $build
        if ($LASTEXITCODE) { throw 'GS source preparation failed' }
        $gsSource=$gsSource.Trim()
    }
    $configure=@('-S',(Join-Path $CollectionRoot 'tools/ps2'),'-B',$build,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release',('-DPS2_GS_SOURCE='+$gsSource),
        ('-DCMAKE_C_COMPILER='+(Get-CollectionTool 'clang-cl')),
        ('-DCMAKE_CXX_COMPILER='+(Get-CollectionTool 'clang-cl')),
        ('-DCMAKE_MAKE_PROGRAM='+(Get-CollectionTool 'ninja')),
        '-DCMAKE_C_FLAGS_RELEASE=/O2 /DNDEBUG /Zi',
        '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Zi /EHsc /fp:precise',
        '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG:FULL /OPT:REF /OPT:ICF',
        '-DPS2_RUNTIME_THINLTO=ON',
        ('-DPS2_BUILD_RUNTIME='+$(if ($ToolsOnly) {'OFF'} else {'ON'})))
    $configure+=@('-DPS2_GAME_GENERATED='+$(if ($ProbeGenerated) {[IO.Path]::GetFullPath($ProbeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_GAME_SYMBOLS='+$(if($FastGameLink){'OFF'}else{'ON'}))
    $configure+=@('-DPS2_IBARA_GENERATED='+$(if ($IbaraGenerated) {[IO.Path]::GetFullPath($IbaraGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_ARCADE_GENERATED='+$(if ($ArcadeGenerated) {[IO.Path]::GetFullPath($ArcadeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_ARRANGE_GENERATED='+$(if ($ArrangeGenerated) {[IO.Path]::GetFullPath($ArrangeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_STAGE_MODULES_MANIFEST='+$(if ($StageModulesManifest) {[IO.Path]::GetFullPath($StageModulesManifest).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_MODE_EXTRAS_MANIFEST='+$(if ($ModeExtrasManifest) {[IO.Path]::GetFullPath($ModeExtrasManifest).Replace('\','/')} else {''}))
    Invoke-CollectionConfigure $cmake $configure $build
    $taskReceiptState=$null
    if($ProbeGenerated -and $StageModulesManifest -and $ModeExtrasManifest -and -not $IbaraGenerated -and -not $FastGameLink) {
        $taskReceiptState=Join-Path $CollectionRoot ('.local/build-inputs/ps2-'+[guid]::NewGuid().ToString('N')+'.json')
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'begin','--state',$taskReceiptState)
    }
    $targets=@('--build',$build,'--parallel',"$Jobs",'--target','ps2_recomp','ps2_analyzer','ps2_vu_contract','ps2_module_contract','ps2_iso_contract','ps2_pad_contract')
    if (-not $ToolsOnly) { $targets+=@('ps2_runtime','ps2_sdk_contract','ps2_kernel_contract','ps2_sdk_codegen_contract','ps2_iop_memory_contract','ps2_spu_contract','ps2_gs_gpu_contract','ps2_gs_transport_contract','ps2_gs_queue_contract','ps2_gs_completion_contract','ps2_gs_trace_contract','ps2_gs_trace_gpu_contract','ps2_native_host_probe') }
    if (-not $ToolsOnly) { $targets+=@('ps2_compact_contract','ps2_memory_access_contract') }
    if ($ProbeGenerated) { $targets+='ps2_espgaluda_probe' }
    if ($IbaraGenerated) { $targets+='ps2_ibara_probe' }
    Invoke-Checked $cmake $targets
    if($taskReceiptState) {
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'finish','--state',$taskReceiptState,'--stage')
    }
    Write-Host "PS2 tools and requested development targets built under $build. No game has been launched."
} finally { $taskProcess.PriorityClass=$previousPriority }
