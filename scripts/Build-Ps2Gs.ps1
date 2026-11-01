#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$gsTaskProcess=Get-Process -Id $PID
$gsPreviousPriority=$gsTaskProcess.PriorityClass
try {
    $gsTaskProcess.PriorityClass='BelowNormal'
    Initialize-CollectionBuildEnvironment
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
    $gsBuild=Join-Path $CollectionRoot 'out/ps2-gs'
    $gsSource=& py -3.12 (Join-Path $PSScriptRoot 'ps2_gs_sources.py') --output $gsBuild
    if($LASTEXITCODE){throw 'GS source preparation failed'}
    $gsCmake=Get-CollectionTool 'cmake'
    $gsConfigure=@('-S',(Join-Path $CollectionRoot 'tools/ps2-gs'),'-B',$gsBuild,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release',('-DPS2_GS_SOURCE='+$gsSource.Trim()),
        ('-DCMAKE_C_COMPILER='+(Get-CollectionTool 'clang-cl')),
        ('-DCMAKE_CXX_COMPILER='+(Get-CollectionTool 'clang-cl')),
        ('-DCMAKE_MAKE_PROGRAM='+(Get-CollectionTool 'ninja')),
        '-DCMAKE_C_FLAGS_RELEASE=/O2 /DNDEBUG /Zi',
        '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Zi /EHsc /fp:precise',
        '-DCMAKE_EXE_LINKER_FLAGS=/DEBUG:FULL /OPT:REF /OPT:ICF')
    Invoke-CollectionConfigure $gsCmake $gsConfigure $gsBuild
    Invoke-Checked $gsCmake @('--build',$gsBuild,'--parallel',"$Jobs",'--target','ps2_gs_gpu_contract','ps2_gs_transport_contract','ps2_gs_queue_contract','ps2_gs_completion_contract','ps2_gs_trace_contract','ps2_gs_trace_gpu_contract','ps2_gs_replay','ps2_gs_window_probe')
} finally { $gsTaskProcess.PriorityClass=$gsPreviousPriority }
