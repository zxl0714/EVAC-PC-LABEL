#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2, [string]$CacheFrom, [switch]$ToolsOnly,
      [string]$ProbeGenerated, [string]$ArcadeGenerated, [string]$ArrangeGenerated,
      [string]$IbaraGenerated, [hashtable]$DaioujouPrograms=@{}, [hashtable]$RTypePrograms=@{},
      [string]$StageModulesManifest, [string]$ModeExtrasManifest, [switch]$RuntimeChecks,
      [switch]$FastGameLink)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskProcess=Get-Process -Id $PID
$previousPriority=$taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass='BelowNormal'
    $taskDaioujouKeys=[ordered]@{boot='PS2_DAIOUJOU_GENERATED';death='PS2_DAIOUJOU_DEATH_GENERATED';gallery='PS2_DAIOUJOU_GALLERY_GENERATED'}
    $taskRTypeKeys=[ordered]@{boot='PS2_RTYPE_FINAL_GENERATED';banks='PS2_RTYPE_FINAL_BANKS';vu='PS2_RTYPE_FINAL_VU'}
    if($RTypePrograms.Count -and ($RTypePrograms.Count -ne 3 -or -not ($RTypePrograms.boot -and $RTypePrograms.banks -and $RTypePrograms.vu))) {
        throw 'R-Type Final requires the reviewed boot program, code banks and VU1 sources'
    }
    if($DaioujouPrograms.Count -and ($DaioujouPrograms.Count -ne 3 -or -not ($DaioujouPrograms.boot -and $DaioujouPrograms.death -and $DaioujouPrograms.gallery))) {
        throw 'DaiOuJou requires generated boot, Death Label and Gallery programs'
    }
    if($RuntimeChecks -and ($ToolsOnly -or $ProbeGenerated -or $ArcadeGenerated -or $ArrangeGenerated -or $IbaraGenerated -or $DaioujouPrograms.Count -or $RTypePrograms.Count -or $StageModulesManifest -or $ModeExtrasManifest)) { throw '-RuntimeChecks builds the independent no-ROM check graph' }
    if($ToolsOnly -and ($ProbeGenerated -or $IbaraGenerated -or $DaioujouPrograms.Count -or $RTypePrograms.Count)) { throw '-ToolsOnly cannot compile the development host' }
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
    # Retain the other configured PS2 game when extending an existing graph.
    if(-not $ToolsOnly -and -not $RuntimeChecks -and (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
        $taskPrevious=@{}
        foreach($line in Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')) {
            if($line -match '^([^/#][^:]*):[^=]+=(.*)$'){$taskPrevious[$Matches[1]]=$Matches[2]}
        }
        if(($IbaraGenerated -or $DaioujouPrograms.Count -or $RTypePrograms.Count) -and -not $ProbeGenerated) {
            $ProbeGenerated=$taskPrevious['PS2_GAME_GENERATED']
            $ArcadeGenerated=$taskPrevious['PS2_ARCADE_GENERATED']
            $ArrangeGenerated=$taskPrevious['PS2_ARRANGE_GENERATED']
            $StageModulesManifest=$taskPrevious['PS2_STAGE_MODULES_MANIFEST']
            $ModeExtrasManifest=$taskPrevious['PS2_MODE_EXTRAS_MANIFEST']
        }
        if(($ProbeGenerated -or $DaioujouPrograms.Count -or $RTypePrograms.Count) -and -not $IbaraGenerated){$IbaraGenerated=$taskPrevious['PS2_IBARA_GENERATED']}
        if(($ProbeGenerated -or $IbaraGenerated -or $RTypePrograms.Count) -and -not $DaioujouPrograms.Count -and $taskPrevious[$taskDaioujouKeys.boot]){
            foreach($name in $taskDaioujouKeys.Keys){$DaioujouPrograms[$name]=$taskPrevious[$taskDaioujouKeys[$name]]}
        }
        if(($ProbeGenerated -or $IbaraGenerated -or $DaioujouPrograms.Count) -and -not $RTypePrograms.Count -and
           $taskPrevious[$taskRTypeKeys.boot] -and $taskPrevious[$taskRTypeKeys.banks] -and $taskPrevious[$taskRTypeKeys.vu]) {
            foreach($name in $taskRTypeKeys.Keys){$RTypePrograms[$name]=$taskPrevious[$taskRTypeKeys[$name]]}
        }
    }
    $gsSource=''
    if (-not $ToolsOnly) {
        $gsSource=& py -3.12 (Join-Path $PSScriptRoot 'ps2_gs_sources.py') --output $build
        if ($LASTEXITCODE) { throw 'GS source preparation failed' }
        $gsSource=$gsSource.Trim()
    }
    $taskMovieSdk=''
    if ($RTypePrograms.Count) {
        $taskMovieSdk=& py -3.12 (Join-Path $PSScriptRoot 'ps2_movie.py') --jobs $Jobs
        if ($LASTEXITCODE) { throw 'PS2 movie decoder build failed' }
        $taskMovieSdk=$taskMovieSdk.Trim()
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
    foreach($name in $taskDaioujouKeys.Keys){
        $program=$DaioujouPrograms[$name]
        $configure+=('-D'+$taskDaioujouKeys[$name]+'='+$(if($program){[IO.Path]::GetFullPath($program).Replace('\','/')}else{''}))
    }
    foreach($name in $taskRTypeKeys.Keys){
        $program=$RTypePrograms[$name]
        $configure+=('-D'+$taskRTypeKeys[$name]+'='+$(if($program){[IO.Path]::GetFullPath($program).Replace('\','/')}else{''}))
    }
    $configure+=('-DPS2_RTYPE_FINAL_FFMPEG='+$taskMovieSdk)
    $configure+=@('-DPS2_ARCADE_GENERATED='+$(if ($ArcadeGenerated) {[IO.Path]::GetFullPath($ArcadeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_ARRANGE_GENERATED='+$(if ($ArrangeGenerated) {[IO.Path]::GetFullPath($ArrangeGenerated).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_STAGE_MODULES_MANIFEST='+$(if ($StageModulesManifest) {[IO.Path]::GetFullPath($StageModulesManifest).Replace('\','/')} else {''}))
    $configure+=@('-DPS2_MODE_EXTRAS_MANIFEST='+$(if ($ModeExtrasManifest) {[IO.Path]::GetFullPath($ModeExtrasManifest).Replace('\','/')} else {''}))
    Invoke-CollectionConfigure $cmake $configure $build
    $taskReceiptState=$null
    if(($IbaraGenerated -or $DaioujouPrograms.Count -or $RTypePrograms.Count -or ($ProbeGenerated -and $StageModulesManifest -and $ModeExtrasManifest)) -and -not $FastGameLink) {
        # Generation receipts identify the translator binary. Finish rebuilding
        # it before taking the snapshot that brackets the runtime build.
        $generationTargets=@('ps2_recomp')
        if($RTypePrograms.Count) { $generationTargets+='ps2_vu_aot' }
        Invoke-Checked $cmake (@('--build',$build,'--parallel',"$Jobs",'--target') + $generationTargets)
        $taskReceiptState=Join-Path $CollectionRoot ('.local/build-inputs/ps2-'+[guid]::NewGuid().ToString('N')+'.json')
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'begin','--state',$taskReceiptState)
    }
    $targets=@('--build',$build,'--parallel',"$Jobs",'--target','ps2_recomp','ps2_analyzer','ps2_vu_contract','ps2_module_contract','ps2_function_table_contract','ps2_iso_contract','ps2_pad_contract')
    if($ToolsOnly -or $RTypePrograms.Count) { $targets+='ps2_vu_aot' }
    if (-not $ToolsOnly) { $targets+=@('ps2_runtime','ps2_sdk_contract','ps2_kernel_contract','ps2_sdk_codegen_contract','ps2_iop_memory_contract','ps2_spu_contract','ps2_gs_gpu_contract','ps2_gs_transport_contract','ps2_gs_queue_contract','ps2_gs_completion_contract','ps2_gs_trace_contract','ps2_gs_trace_gpu_contract','ps2_native_host_probe') }
    if (-not $ToolsOnly) { $targets+=@('ps2_compact_contract','ps2_memory_access_contract','ps2_memory_instruction_contract','ps2_sdl_audio_contract','ps2_descriptor_fallback_contract','ps2_reflex_contract') }
    if ($ProbeGenerated) { $targets+='ps2_espgaluda_probe' }
    if ($IbaraGenerated) { $targets+='ps2_ibara_probe' }
    if ($DaioujouPrograms.Count) { $targets+='ps2_daioujou_probe' }
    if ($RTypePrograms.Count) { $targets+='ps2_rtype_final_probe' }
    Invoke-Checked $cmake $targets
    if($taskReceiptState) {
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'finish','--state',$taskReceiptState,'--stage')
    }
    Write-Host "PS2 tools and requested development targets built under $build. No game has been launched."
} finally { $taskProcess.PriorityClass=$previousPriority }
